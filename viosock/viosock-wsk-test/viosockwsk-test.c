/*
 * Copyright (c) 2023 Virtuozzo International GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** This driver shows how to use the WSK interface for the Virtio Socket Driver.
    The driver creates four client and four server threads. Servers listen
        on predefined ports, clients are randomly connecting to them and
   exchanging serveral messages. Messages are 64 bytes and consist of 32 byte
   long sequence of random bytes plus SHA256 hash of this sequence (to check
   that the message was transferred intact).

        Things worth of noting:
        - the WSK interface is implemented in the viosockwsk library that is
   linked to the test driver,
        - the library needs to be initialized via VioWskModuleInit and finalized
   through VioWskModuleFinit,
        - a device object can be optionally provided to VioWskModuleInit; the
   library uses it as parent for internal workitems and other objects,
        - server threads perform accept() (WskAccept) in non-blocking mode in
   order to be able to correctly terminate themselves when the driver is being
   unloaded (the socket driver does not support accept cancellation),
    - the driver runs its tests infinitely (results can be observer via WPP
   Tracing) until it gets unloaded.
*/

#include "..\inc\debug-utils.h"
#include "..\inc\vio_sockets.h"
#include "..\inc\vio_wsk.h"
#include "..\sys\public.h"
#include "test-messages.h"
#include <bcrypt.h>
#include <ntifs.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <wsk.h>
#ifdef EVENT_TRACING
#include "viosockwsk-test.tmh"
#endif

#define LISTEN_PORT_MIN 1337
#define LISTEN_PORT_MAX 1340
#define SERVER_THREAD_COUNT ((LISTEN_PORT_MAX) - (LISTEN_PORT_MIN) + 1)
#define CLIENT_THREAD_COUNT 16
#define TEST_COUNT 16

#define VIOWSK_TEST_TAG (ULONG)'TKSW'

#define VIOWSK_TEST_FLAG_CONNECTEX 0x1
#define VIOWSK_TEST_FLAG_SOCKCONN 0x2
#define VIOWSK_TEST_FLAG_SENDEX 0x4
#define VIOWSK_TEST_FLAG_RECVEX 0x8
#define VIOWSK_TEST_FLAG_DISCONNECTEX 0x10
#define VIOWSK_TEST_FLAG_MASK                                                  \
  (VIOWSK_TEST_FLAG_CONNECTEX | VIOWSK_TEST_FLAG_SOCKCONN |                    \
   VIOWSK_TEST_FLAG_SENDEX | VIOWSK_TEST_FLAG_RECVEX |                         \
   VIOWSK_TEST_FLAG_DISCONNECTEX)

static volatile LONG _readyThreads;
static volatile LONG _terminate;
static PETHREAD _clientThreads[CLIENT_THREAD_COUNT];
static PETHREAD _serverThreads[SERVER_THREAD_COUNT];
static KEVENT _initEvent;
static PDEVICE_OBJECT _shutdownDeviceObject = NULL;
static WSK_REGISTRATION _vioWskRegistration;
static WSK_PROVIDER_NPI _vioWskProviderNPI;
static WSK_CLIENT_NPI _vioWskClientNPI = {
    NULL,
    NULL,
};

static NTSTATUS _GeneralIrpComplete(_In_ PDEVICE_OBJECT DeviceObject,
                                    _In_ PIRP Irp, _In_ PVOID Context) {
  NTSTATUS status = STATUS_UNSUCCESSFUL;
  DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p; Context=0x%p",
                       DeviceObject, Irp, Context);

  UNREFERENCED_PARAMETER(DeviceObject);
  UNREFERENCED_PARAMETER(Irp);

  KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
  status = STATUS_MORE_PROCESSING_REQUIRED;

  DEBUG_EXIT_FUNCTION("0x%x", status);
  return status;
}

#define WSK_SYNCHRONOUS_CALL(aIrp, aEvent, aCall, aIosb)                       \
  do {                                                                         \
    IoSetCompletionRoutine((aIrp), _GeneralIrpComplete, (aEvent), TRUE, TRUE,  \
                           TRUE);                                              \
    (aIosb)->Status = (aCall);                                                 \
    if ((aIosb)->Status == STATUS_PENDING) {                                   \
      NTSTATUS __waitResult = STATUS_TIMEOUT;                                  \
      LARGE_INTEGER __timeout;                                                 \
                                                                               \
      __timeout.QuadPart = -10000000;                                          \
      do {                                                                     \
        __waitResult = KeWaitForSingleObject((aEvent), Executive, KernelMode,  \
                                             FALSE, &__timeout);               \
        if (__waitResult == STATUS_TIMEOUT &&                                  \
            InterlockedCompareExchange(&_terminate, 1, 1)) {                   \
          IoCancelIrp((aIrp));                                                 \
          break;                                                               \
        }                                                                      \
      } while (__waitResult != STATUS_WAIT_0);                                 \
                                                                               \
      KeWaitForSingleObject((aEvent), Executive, KernelMode, FALSE, NULL);     \
      (aIosb)->Status = (aIrp)->IoStatus.Status;                               \
    }                                                                          \
                                                                               \
    ASSERT((aIosb)->Status == (aIrp)->IoStatus.Status);                        \
    (aIosb)->Information = (aIrp)->IoStatus.Information;                       \
    KeResetEvent((aEvent));                                                    \
    IoReuseIrp((aIrp), STATUS_UNSUCCESSFUL);                                   \
  } while (FALSE)

static NTSTATUS _TestSocket(_In_ PWSK_SOCKET Socket, _In_ PIRP Irp,
                            _Inout_ PVIOWSK_MSG_HASH_OBJECT HashObject,
                            _In_ BOOLEAN Server,
                            _Inout_ volatile LONG *TerminationFlag,
                            _In_ ULONG TestFlags) {
  KEVENT event;
  IO_STATUS_BLOCK iosb;
  WSK_BUF msg;
  PVOID msgFlat = NULL;
  WSK_BUF recvMsg;
  PVOID recvFlat = NULL;
  BOOLEAN verified = FALSE;
  DEBUG_ENTER_FUNCTION(
      "Socket=0x%p; Irp=0x%p; HashObject=0x%p; Server=%u; TestFlags=0x%x",
      Socket, Irp, HashObject, Server, TestFlags);

  memset(&iosb, 0, sizeof(iosb));
  KeInitializeEvent(&event, NotificationEvent, FALSE);
  for (size_t i = 0; i < TEST_COUNT; ++i) {
    if (!NT_SUCCESS(iosb.Status))
      break;

    iosb.Status = VioWskMessageGenerate(NULL, &recvMsg, &recvFlat);
    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("VioWskMessageGenerate: 0x%x", iosb.Status);
      continue;
    }

    iosb.Status = VioWskMessageGenerate(HashObject, &msg, &msgFlat);
    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("Unable to generate test message: 0x%x", iosb.Status);
      goto FreeRecvMessage;
    }

    iosb.Status = VIoWskMessageVerify(HashObject, &msg, &verified);
    if (!NT_SUCCESS(iosb.Status) || !verified) {
      if (!verified) {
        iosb.Status = STATUS_UNSUCCESSFUL;
        DEBUG_ERROR("Generated test message is invalid: 0x%x", iosb.Status);
      } else {
        DEBUG_ERROR("Unable to verify test message: 0x%x", iosb.Status);
      }

      goto FreeMessage;
    }

    if (!Server && (i != 0 || (TestFlags & VIOWSK_TEST_FLAG_CONNECTEX) == 0)) {
      while (NT_SUCCESS(iosb.Status) && msg.Length > 0) {
        if ((TestFlags & VIOWSK_TEST_FLAG_SENDEX) != 0)
          WSK_SYNCHRONOUS_CALL(
              Irp, &event,
              ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                  ->WskSendEx(Socket, &msg, 0, 0, NULL, Irp),
              &iosb);
        else
          WSK_SYNCHRONOUS_CALL(
              Irp, &event,
              ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                  ->WskSend(Socket, &msg, 0, Irp),
              &iosb);

        if (iosb.Status == STATUS_CANT_WAIT) {
          LARGE_INTEGER timeout;

          DEBUG_WARNING("Cannot wait for receive, sleeping\n");
          timeout.QuadPart = -10000000;
          KeDelayExecutionThread(KernelMode, FALSE, &timeout);
          if (InterlockedCompareExchange(TerminationFlag, 1, 1))
            break;

          iosb.Status = STATUS_SUCCESS;
          continue;
        }

        if (!NT_SUCCESS(iosb.Status) || iosb.Information != msg.Length) {
          DEBUG_ERROR("Unable to send the test message: 0x%x (%Iu sent, %Iu "
                      "requested)\n",
                      iosb.Status, iosb.Information, msg.Length);
          goto FreeMessage;
        }

        DEBUG_INFO("%Iu bytes sent (0x%x)\n", iosb.Information, iosb.Status);
        VioWskMessageAdvance(&msg, iosb.Information);
      }
    }

    while (NT_SUCCESS(iosb.Status) && recvMsg.Length > 0) {
      if ((TestFlags & VIOWSK_TEST_FLAG_RECVEX) != 0)
        WSK_SYNCHRONOUS_CALL(
            Irp, &event,
            ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                ->WskReceiveEx(Socket, &recvMsg, 0, NULL, NULL, NULL, Irp),
            &iosb);
      else
        WSK_SYNCHRONOUS_CALL(
            Irp, &event,
            ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                ->WskReceive(Socket, &recvMsg, 0, Irp),
            &iosb);

      if (iosb.Status == STATUS_CANT_WAIT) {
        LARGE_INTEGER timeout;

        DEBUG_WARNING("Cannot wait for receive, sleeping\n");
        timeout.QuadPart = -10000000;
        KeDelayExecutionThread(KernelMode, FALSE, &timeout);
        if (InterlockedCompareExchange(TerminationFlag, 1, 1))
          break;

        iosb.Status = STATUS_SUCCESS;
        continue;
      }

      if (!NT_SUCCESS(iosb.Status) || recvMsg.Length != iosb.Information) {
        DEBUG_ERROR("Unable to receive the tes message: 0x%x (%Iu bytes "
                    "length, %Iu bytes received)",
                    iosb.Status, recvMsg.Length, iosb.Information);
        goto FreeMessage;
      }

      DEBUG_INFO("%Iu bytes received (0x%x)\n", iosb.Information, iosb.Status);
      VioWskMessageAdvance(&recvMsg, iosb.Information);
    }

    if (NT_SUCCESS(iosb.Status)) {
      iosb.Status = VIoWskMessageVerifyBuffer(HashObject, recvFlat, &verified);
      if (!NT_SUCCESS(iosb.Status) || !verified) {
        if (!verified)
          iosb.Status = STATUS_UNSUCCESSFUL;

        DEBUG_ERROR("Unable to verify test message: 0x%x", iosb.Status);
        goto SendMessage;
      }
    }

  SendMessage:
    if (Server && ((i != TEST_COUNT - 1) ||
                   (TestFlags & VIOWSK_TEST_FLAG_DISCONNECTEX) == 0)) {
      while (NT_SUCCESS(iosb.Status) && msg.Length > 0) {
        if ((TestFlags & VIOWSK_TEST_FLAG_SENDEX) != 0)
          WSK_SYNCHRONOUS_CALL(
              Irp, &event,
              ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                  ->WskSendEx(Socket, &msg, 0, 0, NULL, Irp),
              &iosb);
        else
          WSK_SYNCHRONOUS_CALL(
              Irp, &event,
              ((PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch)
                  ->WskSend(Socket, &msg, 0, Irp),
              &iosb);

        if (iosb.Status == STATUS_CANT_WAIT) {
          LARGE_INTEGER timeout;

          DEBUG_WARNING("Cannot wait for receive, sleeping\n");
          timeout.QuadPart = -10000000;
          KeDelayExecutionThread(KernelMode, FALSE, &timeout);
          if (InterlockedCompareExchange(TerminationFlag, 1, 1))
            break;

          iosb.Status = STATUS_SUCCESS;
          continue;
        }

        if (!NT_SUCCESS(iosb.Status) || iosb.Information != msg.Length) {
          DEBUG_ERROR("Unable to send the test message: 0x%x (%Iu sent, %Iu "
                      "requested)\n",
                      iosb.Status, iosb.Information, msg.Length);
          goto FreeMessage;
        }

        DEBUG_INFO("%Iu bytes sent (0x%x)\n", iosb.Information, iosb.Status);
        VioWskMessageAdvance(&msg, iosb.Information);
      }
    }
  FreeMessage:
    VioWskMessageFree(&msg, msgFlat);
  FreeRecvMessage:
    VioWskMessageFree(&recvMsg, recvFlat);
  }

  if (Server) {
    DEBUG_INFO("Server thread test finished (flags 0x%x, status 0x%x)\n",
               TestFlags, iosb.Status);
  }

  DEBUG_EXIT_FUNCTION("0x%x", iosb.Status);
  return iosb.Status;
}

typedef struct _TEST_THREAD_CONTEXT {
  LIST_ENTRY Entry;
  PKSPIN_LOCK ListLock;
  PETHREAD Thread;
  PIRP Irp;
  PWSK_SOCKET Socket;
  VIOWSK_MSG_HASH_OBJECT Hash;
  volatile LONG Terminated;
  ULONG TestFlags;
  WSK_BUF FarewellMsg;
  PVOID FarewellMsgFlat;
} TEST_THREAD_CONTEXT, *PTEST_THREAD_CONTEXT;

static NTSTATUS _SocketTestThreadCreate(_In_ PWSK_SOCKET Socket,
                                        _In_ PLIST_ENTRY ListHead,
                                        _In_ PKSPIN_LOCK ListLock,
                                        _In_ ULONG TestFlags);

static void _TestThreadRoutine(_In_ PVOID Context) {
  KIRQL irql;
  KEVENT event;
  IO_STATUS_BLOCK iosb;
  BOOLEAN isActive = FALSE;
  NTSTATUS Status = STATUS_UNSUCCESSFUL;
  PTEST_THREAD_CONTEXT ctx = (PTEST_THREAD_CONTEXT)Context;
  DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

  Status = _TestSocket(ctx->Socket, ctx->Irp, &ctx->Hash, TRUE,
                       &ctx->Terminated, ctx->TestFlags);
  KeAcquireSpinLock(ctx->ListLock, &irql);
  if (!IsListEmpty(&ctx->Entry)) {
    RemoveEntryList(&ctx->Entry);
    isActive = TRUE;
  }

  KeReleaseSpinLock(ctx->ListLock, irql);
  if (isActive) {
    memset(&iosb, 0, sizeof(iosb));
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    WSK_SYNCHRONOUS_CALL(
        ctx->Irp, &event,
        ((PWSK_PROVIDER_CONNECTION_DISPATCH)ctx->Socket->Dispatch)
            ->WskDisconnect(ctx->Socket, &ctx->FarewellMsg, 0, ctx->Irp),
        &iosb);
    if (iosb.Status == STATUS_CONNECTION_INVALID)
      iosb.Status = STATUS_SUCCESS;

    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("Unable to disconnect the server socket: 0x%x", iosb.Status);
    }

    WSK_SYNCHRONOUS_CALL(ctx->Irp, &event,
                         ((PWSK_PROVIDER_BASIC_DISPATCH)ctx->Socket->Dispatch)
                             ->WskCloseSocket(ctx->Socket, ctx->Irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("Unable to close the server socket: 0x%x", iosb.Status);
    }

    ObDereferenceObject(ctx->Thread);
    IoFreeIrp(ctx->Irp);
    if (ctx->FarewellMsgFlat)
      VioWskMessageFree(&ctx->FarewellMsg, ctx->FarewellMsgFlat);

    VIoWskMessageDestroyHashObject(&ctx->Hash);
    ExFreePoolWithTag(ctx, VIOWSK_TEST_TAG);
  }

  DEBUG_EXIT_FUNCTION("0x%x", Status);
  return;
}

static void _ServerThreadRoutine(_In_opt_ PVOID Context) {
  KIRQL irql;
  KEVENT event;
  ULONG nonBlocking = 0;
  PIRP irp = NULL;
  SOCKADDR_VM listenAddress;
  DECLARE_UNICODE_STRING_SIZE(listenHost, 16);
  DECLARE_UNICODE_STRING_SIZE(listenPort, 16);
  PADDRINFOEXW addrInfo = NULL;
  PWSK_SOCKET serverSocket = NULL;
  IO_STATUS_BLOCK iosb;
  ULONG testFlags = 0;
  KSPIN_LOCK threadListLock;
  LIST_ENTRY threadListHead;
  DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

  if (InterlockedIncrement(&_readyThreads) ==
      (CLIENT_THREAD_COUNT + SERVER_THREAD_COUNT))
    KeSetEvent(&_initEvent, IO_NO_INCREMENT, FALSE);

  irp = IoAllocateIrp(1, FALSE);
  if (!irp) {
    iosb.Status = STATUS_INSUFFICIENT_RESOURCES;
    DEBUG_ERROR("Unable to allocate IRP: 0x%x", iosb.Status);
    goto Exit;
  }

  KeInitializeEvent(&event, NotificationEvent, FALSE);
  WSK_SYNCHRONOUS_CALL(irp, &event,
                       _vioWskProviderNPI.Dispatch->WskSocket(
                           _vioWskProviderNPI.Client, AF_VSOCK, SOCK_STREAM, 0,
                           WSK_FLAG_LISTEN_SOCKET, NULL, NULL, NULL, NULL, NULL,
                           irp),
                       &iosb);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to create server socket: 0x%x", iosb.Status);
    goto FreeIrp;
  }

  serverSocket = (PWSK_SOCKET)iosb.Information;
  iosb.Status =
      RtlUnicodeStringPrintf(&listenHost, L"%u", (ULONG)VMADDR_CID_ANY);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to prepare server address hos: 0x%x", iosb.Status);
    goto CloseSocket;
  }

  iosb.Status = RtlUnicodeStringPrintf(&listenPort, L"%Iu",
                                       LISTEN_PORT_MIN + (SIZE_T)Context);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to prepare server address port: 0x%x", iosb.Status);
    goto CloseSocket;
  }

  WSK_SYNCHRONOUS_CALL(irp, &event,
                       _vioWskProviderNPI.Dispatch->WskGetAddressInfo(
                           _vioWskProviderNPI.Client, &listenHost, &listenPort,
                           0, NULL, NULL, &addrInfo, NULL, NULL, irp),
                       &iosb);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to translate the listen address: 0x%x", iosb.Status);
    goto CloseSocket;
  }

  listenAddress = *(PSOCKADDR_VM)addrInfo->ai_addr;
  _vioWskProviderNPI.Dispatch->WskFreeAddressInfo(_vioWskProviderNPI.Client,
                                                  addrInfo);

  WSK_SYNCHRONOUS_CALL(
      irp, &event,
      ((PWSK_PROVIDER_LISTEN_DISPATCH)serverSocket->Dispatch)
          ->WskBind(serverSocket, (PSOCKADDR)&listenAddress, 0, irp),
      &iosb);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to bind: 0x%x", iosb.Status);
    goto CloseSocket;
  }

  nonBlocking = TRUE;
  WSK_SYNCHRONOUS_CALL(irp, &event,
                       ((PWSK_PROVIDER_LISTEN_DISPATCH)serverSocket->Dispatch)
                           ->WskControlSocket(serverSocket, WskIoctl, FIONBIO,
                                              0, sizeof(nonBlocking),
                                              &nonBlocking, 0, NULL, NULL, irp),
                       &iosb);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to be nonblocking: 0x%x", iosb.Status);
    goto CloseSocket;
  }

  nonBlocking = FALSE;
  InitializeListHead(&threadListHead);
  KeInitializeSpinLock(&threadListLock);
  while (!InterlockedCompareExchange(&_terminate, 1, 1)) {
    PWSK_SOCKET clientSocket = NULL;
    SOCKADDR_VM localAddr;
    SOCKADDR_VM remoteAddr;
    LARGE_INTEGER timeout;

    timeout.QuadPart = -10000000;
    WSK_SYNCHRONOUS_CALL(
        irp, &event,
        ((PWSK_PROVIDER_LISTEN_DISPATCH)serverSocket->Dispatch)
            ->WskAccept(serverSocket, WSK_FLAG_CONNECTION_SOCKET, NULL, NULL,
                        (PSOCKADDR)&localAddr, (PSOCKADDR)&remoteAddr, irp),
        &iosb);
    if (iosb.Status == STATUS_CANT_WAIT) {
      KeDelayExecutionThread(KernelMode, FALSE, &timeout);
      continue;
    }

    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("Unable to accept: 0x%x", iosb.Status);
      break;
    }

    clientSocket = (PWSK_SOCKET)iosb.Information;
    WSK_SYNCHRONOUS_CALL(irp, &event,
                         ((PWSK_PROVIDER_LISTEN_DISPATCH)clientSocket->Dispatch)
                             ->WskControlSocket(clientSocket, WskIoctl, FIONBIO,
                                                0, sizeof(nonBlocking),
                                                &nonBlocking, 0, NULL, NULL,
                                                irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status)) {
      DEBUG_ERROR("Unable to be blocking: 0x%x", iosb.Status);
    }

    if (NT_SUCCESS(iosb.Status)) {
      WSK_SYNCHRONOUS_CALL(irp, &event,
                           _vioWskProviderNPI.Dispatch->WskGetNameInfo(
                               _vioWskProviderNPI.Client, (PSOCKADDR)&localAddr,
                               sizeof(localAddr), &listenHost, &listenPort, 0,
                               NULL, NULL, irp),
                           &iosb);
      if (NT_SUCCESS(iosb.Status)) {
        DEBUG_INFO("Accepted connection:");
        DEBUG_INFO("  Local: %wZ:%wZ", &listenHost, &listenPort);
        WSK_SYNCHRONOUS_CALL(irp, &event,
                             _vioWskProviderNPI.Dispatch->WskGetNameInfo(
                                 _vioWskProviderNPI.Client,
                                 (PSOCKADDR)&remoteAddr, sizeof(remoteAddr),
                                 &listenHost, &listenPort, 0, NULL, NULL, irp),
                             &iosb);
        if (!NT_SUCCESS(iosb.Status)) {
          DEBUG_ERROR("Unable to get client address strings: 0x%x",
                      iosb.Status);
        }
      }
    }

    if (NT_SUCCESS(iosb.Status)) {
      DEBUG_INFO("  Remote: %wZ:%wZ", &listenHost, &listenPort);
      iosb.Status = _SocketTestThreadCreate(clientSocket, &threadListHead,
                                            &threadListLock, testFlags);
      testFlags = (testFlags + 1) % (VIOWSK_TEST_FLAG_MASK + 1);
      if (!NT_SUCCESS(iosb.Status)) {
        DEBUG_ERROR("Socket test thread failed to create: 0x%x", iosb.Status);
      }
    }

    if (!NT_SUCCESS(iosb.Status)) {
      WSK_SYNCHRONOUS_CALL(
          irp, &event,
          ((PWSK_PROVIDER_BASIC_DISPATCH)clientSocket->Dispatch)
              ->WskCloseSocket(clientSocket, irp),
          &iosb);
      break;
    }
  }

  KeAcquireSpinLock(&threadListLock, &irql);
  while (!IsListEmpty(&threadListHead)) {
    PTEST_THREAD_CONTEXT ctx =
        CONTAINING_RECORD(threadListHead.Flink, TEST_THREAD_CONTEXT, Entry);

    RemoveEntryList(&ctx->Entry);
    InitializeListHead(&ctx->Entry);
    KeReleaseSpinLock(&threadListLock, irql);
    InterlockedExchange(&ctx->Terminated, 1);
    KeWaitForSingleObject(ctx->Thread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(ctx->Thread);
    WSK_SYNCHRONOUS_CALL(irp, &event,
                         ((PWSK_PROVIDER_BASIC_DISPATCH)ctx->Socket->Dispatch)
                             ->WskCloseSocket(ctx->Socket, irp),
                         &iosb);
    IoFreeIrp(ctx->Irp);
    if (ctx->FarewellMsgFlat)
      VioWskMessageFree(&ctx->FarewellMsg, ctx->FarewellMsgFlat);

    VIoWskMessageDestroyHashObject(&ctx->Hash);
    ExFreePoolWithTag(ctx, VIOWSK_TEST_TAG);
    KeAcquireSpinLock(&threadListLock, &irql);
  }

  KeReleaseSpinLock(&threadListLock, irql);
CloseSocket:
  WSK_SYNCHRONOUS_CALL(irp, &event,
                       ((PWSK_PROVIDER_BASIC_DISPATCH)serverSocket->Dispatch)
                           ->WskCloseSocket(serverSocket, irp),
                       &iosb);
  if (!NT_SUCCESS(iosb.Status)) {
    DEBUG_ERROR("Unable to close the server socket: 0x%x", iosb.Status);
  }
FreeIrp:
  IoFreeIrp(irp);
Exit:
  InterlockedExchange(&_terminate, 1);

  DEBUG_EXIT_FUNCTION("0x%x", iosb.Status);
  return;
}

static void _ClientThreadRoutine(_In_opt_ PVOID Context) {
  KEVENT event;
  PIRP irp = NULL;
  LARGE_INTEGER timeSeed;
  LARGE_INTEGER timeout;
  IO_STATUS_BLOCK iosb;
  SOCKADDR_VM localAddr;
  SOCKADDR_VM remoteAddr;
  PWSK_SOCKET socket = NULL;
  VIOWSK_MSG_HASH_OBJECT ho;
  DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

  UNREFERENCED_PARAMETER(Context);

  if (InterlockedIncrement(&_readyThreads) ==
      (CLIENT_THREAD_COUNT + SERVER_THREAD_COUNT))
    KeSetEvent(&_initEvent, IO_NO_INCREMENT, FALSE);

  iosb.Status = VioWskMessageCreateHashObject(&ho);
  if (!NT_SUCCESS(iosb.Status))
    goto Exit;

  irp = IoAllocateIrp(1, FALSE);
  if (!irp) {
    iosb.Status = STATUS_INSUFFICIENT_RESOURCES;
    DEBUG_ERROR("Failed to allocate IRP: 0x%x", iosb.Status);
    goto FreeHashObject;
  }

  timeout.QuadPart = -10000000;
  KeQuerySystemTime(&timeSeed);
  KeInitializeEvent(&event, NotificationEvent, FALSE);
  while (!InterlockedCompareExchange(&_terminate, 1, 1)) {
    for (ULONG i = 0; i < VIOWSK_TEST_FLAG_MASK + 1; ++i) {
      ULONG testFlags = i;
      ULONG testStatus = STATUS_SUCCESS;

      if (InterlockedCompareExchange(&_terminate, 1, 1))
        break;

      if (((testFlags & VIOWSK_TEST_FLAG_SOCKCONN) != 0) &&
          ((testFlags & VIOWSK_TEST_FLAG_CONNECTEX) != 0))
        testFlags &= ~VIOWSK_TEST_FLAG_SOCKCONN;

      socket = NULL;
      memset(&localAddr, 0, sizeof(localAddr));
      localAddr.svm_family = AF_VSOCK;
      localAddr.svm_cid = (UINT)VMADDR_CID_ANY;
      localAddr.svm_port = (UINT)VMADDR_PORT_ANY;
      memset(&remoteAddr, 0, sizeof(remoteAddr));
      remoteAddr.svm_family = AF_VSOCK;
      remoteAddr.svm_cid = (UINT)VMADDR_CID_ANY;
      remoteAddr.svm_port = LISTEN_PORT_MIN + (RtlRandomEx(&timeSeed.LowPart) %
                                               (SERVER_THREAD_COUNT));
      if ((testFlags & VIOWSK_TEST_FLAG_SOCKCONN) != 0) {
        do {
          WSK_SYNCHRONOUS_CALL(irp, &event,
                               _vioWskProviderNPI.Dispatch->WskSocketConnect(
                                   _vioWskProviderNPI.Client, SOCK_STREAM, 0,
                                   (PSOCKADDR)&localAddr,
                                   (PSOCKADDR)&remoteAddr,
                                   WSK_FLAG_CONNECTION_SOCKET, NULL, NULL, NULL,
                                   NULL, NULL, irp),
                               &iosb);
          if (!NT_SUCCESS(iosb.Status)) {
            DEBUG_ERROR("Unable to create client socket and connect it: 0x%x",
                        iosb.Status);
            break;
          }

          socket = (PWSK_SOCKET)iosb.Information;
        } while (FALSE);
      } else {
        do {
          WSK_SYNCHRONOUS_CALL(irp, &event,
                               _vioWskProviderNPI.Dispatch->WskSocket(
                                   _vioWskProviderNPI.Client, AF_VSOCK,
                                   SOCK_STREAM, 0, WSK_FLAG_CONNECTION_SOCKET,
                                   NULL, NULL, NULL, NULL, NULL, irp),
                               &iosb);
          if (!NT_SUCCESS(iosb.Status)) {
            DEBUG_ERROR("Unable to create client socket: 0x%x", iosb.Status);
            break;
          }

          socket = (PWSK_SOCKET)iosb.Information;
          WSK_SYNCHRONOUS_CALL(
              irp, &event,
              ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                  ->WskBind(socket, (PSOCKADDR)&localAddr, 0, irp),
              &iosb);
        } while (FALSE);

        if (NT_SUCCESS(iosb.Status)) {
          if ((testFlags & VIOWSK_TEST_FLAG_CONNECTEX) != 0) {
            WSK_BUF msg;
            PVOID msgFlat = NULL;
            BOOLEAN verified = FALSE;

            memset(&msg, 0, sizeof(msg));
            do {
              iosb.Status = VioWskMessageGenerate(&ho, &msg, &msgFlat);
              if (!NT_SUCCESS(iosb.Status)) {
                DEBUG_ERROR("Failed to generate a message: 0x%x", iosb.Status);
                break;
              }

              iosb.Status = VIoWskMessageVerify(&ho, &msg, &verified);
              if (!NT_SUCCESS(iosb.Status)) {
                DEBUG_ERROR("Failed to verify a message: 0x%x", iosb.Status);
                break;
              }

              if (!verified) {
                DEBUG_ERROR("Message hash mismatch: 0x%x", iosb.Status);
                break;
              }

              WSK_SYNCHRONOUS_CALL(
                  irp, &event,
                  ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                      ->WskConnectEx(socket, (PSOCKADDR)&remoteAddr, &msg, 0,
                                     irp),
                  &iosb);
              if (NT_SUCCESS(iosb.Status)) {
                DEBUG_INFO("%Iu bytes sent (0x%x)\n", iosb.Information,
                           iosb.Status);
                VioWskMessageAdvance(&msg, iosb.Information);
                while (NT_SUCCESS(iosb.Status) && msg.Length > 0) {
                  if ((testFlags & VIOWSK_TEST_FLAG_SENDEX) != 0)
                    WSK_SYNCHRONOUS_CALL(
                        irp, &event,
                        ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                            ->WskSendEx(socket, &msg, 0, 0, NULL, irp),
                        &iosb);
                  else
                    WSK_SYNCHRONOUS_CALL(
                        irp, &event,
                        ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                            ->WskSend(socket, &msg, 0, irp),
                        &iosb);

                  if (iosb.Status == STATUS_CANT_WAIT) {
                    DEBUG_WARNING("Cannot wait for receive, sleeping\n");
                    KeDelayExecutionThread(KernelMode, FALSE, &timeout);
                    if (InterlockedCompareExchange(&_terminate, 1, 1))
                      break;

                    iosb.Status = STATUS_SUCCESS;
                    continue;
                  }

                  if (!NT_SUCCESS(iosb.Status) ||
                      iosb.Information != msg.Length) {
                    DEBUG_ERROR("Unable to send the test message: 0x%x (%Iu "
                                "sent, %Iu requested)\n",
                                iosb.Status, iosb.Information, msg.Length);
                    break;
                  }

                  DEBUG_INFO("%Iu bytes sent (0x%x)\n", iosb.Information,
                             iosb.Status);
                  VioWskMessageAdvance(&msg, iosb.Information);
                }
              }
            } while (FALSE);

            if (msgFlat)
              VioWskMessageFree(&msg, msgFlat);
          } else
            WSK_SYNCHRONOUS_CALL(
                irp, &event,
                ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                    ->WskConnect(socket, (PSOCKADDR)&remoteAddr, 0, irp),
                &iosb);
        }
      }

      if (NT_SUCCESS(iosb.Status)) {
        iosb.Status =
            _TestSocket(socket, irp, &ho, FALSE, &_terminate, testFlags);
        if (!NT_SUCCESS(iosb.Status)) {
          testStatus = iosb.Status;
          DEBUG_ERROR("Client socket test failed: 0x%x", iosb.Status);
        }

        WSK_SYNCHRONOUS_CALL(
            irp, &event,
            ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                ->WskDisconnect(socket, NULL, 0, irp),
            &iosb);
        if (iosb.Status == STATUS_CONNECTION_INVALID)
          iosb.Status = STATUS_SUCCESS;

        if (!NT_SUCCESS(iosb.Status)) {
          testStatus = iosb.Status;
          DEBUG_ERROR("Client socket disconnect failed: 0x%x", iosb.Status);
        }
      } else {
        testStatus = iosb.Status;
        DEBUG_ERROR("Unable to connect to the server: 0x%x", iosb.Status);
      }

      if (socket != NULL) {
        WSK_SYNCHRONOUS_CALL(
            irp, &event,
            ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)
                ->WskCloseSocket(socket, irp),
            &iosb);
        if (!NT_SUCCESS(iosb.Status)) {
          DEBUG_ERROR("Unable to close the client socket: 0x%x", iosb.Status);
        }
      }

      DEBUG_INFO("Client thread test finished (flags 0x%x, status 0x%x)\n",
                 testFlags, testStatus);
    }

    KeDelayExecutionThread(KernelMode, FALSE, &timeout);
  }

  IoFreeIrp(irp);
FreeHashObject:
  VIoWskMessageDestroyHashObject(&ho);
Exit:
  InterlockedExchange(&_terminate, 1);

  DEBUG_EXIT_FUNCTION("0x%x", iosb.Status);
  return;
}

static void _DestroyThreadGroup(_In_ PETHREAD *ObjectArray, _In_ SIZE_T Count) {
  DEBUG_ENTER_FUNCTION("ObjectArray=0x%p; Count=%Iu", ObjectArray, Count);

  InterlockedExchange(&_terminate, 1);
  for (SIZE_T i = 0; i < Count; ++i) {
    KeWaitForSingleObject(ObjectArray[i], Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(ObjectArray[i]);
  }

  DEBUG_EXIT_FUNCTION_VOID();
  return;
}

static NTSTATUS _CreateThreadGroup(_In_ SIZE_T Count,
                                   _In_ PKSTART_ROUTINE Routine,
                                   _Out_ PETHREAD *ObjectArray) {
  CLIENT_ID clientId;
  OBJECT_ATTRIBUTES oa;
  HANDLE hThread = NULL;
  NTSTATUS Status = STATUS_UNSUCCESSFUL;
#pragma warning(push)
#pragma warning(disable : 4152)
  DEBUG_ENTER_FUNCTION("Count=%Iu; Routine=0x%p; ObjectArray=0x%p", Count,
                       Routine, ObjectArray);
#pragma warning(pop)

  InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
  for (SIZE_T i = 0; i < Count; ++i) {
    Status = PsCreateSystemThread(&hThread, SYNCHRONIZE, &oa, NULL, &clientId,
                                  Routine, (PVOID)i);
    if (NT_SUCCESS(Status)) {
      Status = ObReferenceObjectByHandle(hThread, SYNCHRONIZE, *PsThreadType,
                                         KernelMode, ObjectArray + i, NULL);
      ZwClose(hThread);
    }

    if (!NT_SUCCESS(Status)) {
      _DestroyThreadGroup(ObjectArray, i);
      break;
    }
  }

  DEBUG_EXIT_FUNCTION("0x%x", Status);
  return Status;
}

static NTSTATUS _SocketTestThreadCreate(_In_ PWSK_SOCKET Socket,
                                        _In_ PLIST_ENTRY ListHead,
                                        _In_ PKSPIN_LOCK ListLock,
                                        _In_ ULONG TestFlags) {
  KIRQL irql;
  CLIENT_ID clientId;
  OBJECT_ATTRIBUTES oa;
  HANDLE hThread = NULL;
  PTEST_THREAD_CONTEXT ctx = NULL;
  NTSTATUS Status = STATUS_UNSUCCESSFUL;
  DEBUG_ENTER_FUNCTION(
      "Socket=0x%p; ListHead=0x%p; ListLock=0x%p; TestFlags=0x%x", Socket,
      ListHead, ListLock, TestFlags);

  ctx = ExAllocatePoolUninitialized(NonPagedPool, sizeof(TEST_THREAD_CONTEXT),
                                    VIOWSK_TEST_TAG);
  if (!ctx) {
    Status = STATUS_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  memset(ctx, 0, sizeof(TEST_THREAD_CONTEXT));
  Status = VioWskMessageCreateHashObject(&ctx->Hash);
  if (!NT_SUCCESS(Status))
    goto FreeCtx;

  if ((TestFlags & VIOWSK_TEST_FLAG_DISCONNECTEX) != 0) {
    Status = VioWskMessageGenerate(&ctx->Hash, &ctx->FarewellMsg,
                                   &ctx->FarewellMsgFlat);
    if (!NT_SUCCESS(Status))
      goto FreeHashObject;
  }

  InterlockedExchange(&ctx->Terminated, 0);
  InitializeListHead(&ctx->Entry);
  ctx->ListLock = ListLock;
  ctx->Socket = Socket;
  ctx->TestFlags = TestFlags;
  ctx->Irp = IoAllocateIrp(1, FALSE);
  if (!ctx->Irp) {
    Status = STATUS_INSUFFICIENT_RESOURCES;
    goto FreeHashObject;
  }

  KeAcquireSpinLock(ListLock, &irql);
  InsertTailList(ListHead, &ctx->Entry);
  KeReleaseSpinLock(ListLock, irql);
  InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
  Status = PsCreateSystemThread(&hThread, SYNCHRONIZE, &oa, NULL, &clientId,
                                _TestThreadRoutine, ctx);
  if (!NT_SUCCESS(Status))
    goto FreeIrp;
  ;

  Status = ObReferenceObjectByHandle(hThread, SYNCHRONIZE, *PsThreadType,
                                     KernelMode, &ctx->Thread, NULL);
  if (!NT_SUCCESS(Status)) {
    InterlockedExchange(&ctx->Terminated, 1);
    ZwWaitForSingleObject(hThread, FALSE, NULL);
    goto CloseThread;
  }

  ctx = NULL;
CloseThread:
  ZwClose(hThread);
  hThread = NULL;
FreeIrp:
  if (hThread) {
    KeAcquireSpinLock(ListLock, &irql);
    RemoveEntryList(&ctx->Entry);
    InitializeListHead(&ctx->Entry);
    KeReleaseSpinLock(ListLock, irql);
  }

  if (ctx && ctx->Irp)
    IoFreeIrp(ctx->Irp);
FreeHashObject:
  if (ctx) {
    if (ctx->FarewellMsgFlat)
      VioWskMessageFree(&ctx->FarewellMsg, ctx->FarewellMsgFlat);

    VIoWskMessageDestroyHashObject(&ctx->Hash);
  }
FreeCtx:
  if (ctx)
    ExFreePoolWithTag(ctx, VIOWSK_TEST_TAG);
Exit:
  DEBUG_EXIT_FUNCTION("0x%x", Status);
  return Status;
}

static void DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
  DEBUG_ENTER_FUNCTION("DriverObject=0x%p", DriverObject);

  UNREFERENCED_PARAMETER(DriverObject);

  _DestroyThreadGroup(_clientThreads,
                      sizeof(_clientThreads) / sizeof(_clientThreads[0]));
  _DestroyThreadGroup(_serverThreads,
                      sizeof(_serverThreads) / sizeof(_serverThreads[0]));
  VioWskReleaseProviderNPI(&_vioWskRegistration);
  VioWskDeregister(&_vioWskRegistration);
  VioWskModuleFinit();
  IoDeleteDevice(_shutdownDeviceObject);
  VioWskMessageModuleFinit();

  DEBUG_EXIT_FUNCTION_VOID();
  WPP_CLEANUP(DriverObject);
  return;
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath) {
  PDEVICE_OBJECT Device = NULL;
  NTSTATUS Status = STATUS_UNSUCCESSFUL;
  WPP_INIT_TRACING(DriverObject, RegistryPath);
  DEBUG_ENTER_FUNCTION("DriverObject=0x%p; RegistryPath=\"%wZ\"", DriverObject,
                       RegistryPath);

  Status = VioWskMessageModuleInit();
  if (!NT_SUCCESS(Status))
    goto Exit;

  Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE,
                          &Device);
  if (!NT_SUCCESS(Status))
    goto FinalizeMsgModule;

  Status = VioWskModuleInit(DriverObject, RegistryPath, Device);
  if (!NT_SUCCESS(Status))
    goto DeleteDevice;

  Status = VioWskRegister(&_vioWskClientNPI, &_vioWskRegistration);
  if (!NT_SUCCESS(Status))
    goto VioWskFinit;

  Status = VioWskCaptureProviderNPI(&_vioWskRegistration, WSK_INFINITE_WAIT,
                                    &_vioWskProviderNPI);
  if (!NT_SUCCESS(Status))
    goto VioWskDeregister;

  KeInitializeEvent(&_initEvent, NotificationEvent, FALSE);
  Status =
      _CreateThreadGroup(sizeof(_serverThreads) / sizeof(_serverThreads[0]),
                         _ServerThreadRoutine, _serverThreads);
  if (!NT_SUCCESS(Status))
    goto VIoWskReleaseNPI;

  Status =
      _CreateThreadGroup(sizeof(_clientThreads) / sizeof(_clientThreads[0]),
                         _ClientThreadRoutine, _clientThreads);
  if (!NT_SUCCESS(Status))
    goto DestroyServers;

  KeWaitForSingleObject(&_initEvent, Executive, KernelMode, FALSE, NULL);
  if (_terminate) {
    Status = STATUS_UNSUCCESSFUL;
    goto DestroyClients;
  }

  DriverObject->DriverUnload = DriverUnload;
  _shutdownDeviceObject =
      (PDEVICE_OBJECT)InterlockedExchangePointer(&Device, NULL);
DestroyClients:
  if (!NT_SUCCESS(Status))
    _DestroyThreadGroup(_clientThreads,
                        sizeof(_clientThreads) / sizeof(_clientThreads[0]));
DestroyServers:
  if (!NT_SUCCESS(Status))
    _DestroyThreadGroup(_serverThreads,
                        sizeof(_serverThreads) / sizeof(_serverThreads[0]));
VIoWskReleaseNPI:
  if (!NT_SUCCESS(Status))
    VioWskReleaseProviderNPI(&_vioWskRegistration);
VioWskDeregister:
  if (!NT_SUCCESS(Status))
    VioWskDeregister(&_vioWskRegistration);
VioWskFinit:
  if (!NT_SUCCESS(Status))
    VioWskModuleFinit();
DeleteDevice:
  if (Device)
    IoDeleteDevice(Device);
FinalizeMsgModule:
  if (!NT_SUCCESS(Status))
    VioWskMessageModuleFinit();
Exit:
  if (!NT_SUCCESS(Status)) {
    WPP_CLEANUP(DriverObject);
  }

  DEBUG_EXIT_FUNCTION("0x%x", Status);
  return Status;
}