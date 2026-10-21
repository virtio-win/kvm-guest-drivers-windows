/*
 * Copyright (c) 2026 Virtuozzo International GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/** Kernel-mode counterpart of vsock_perf built on top of the viosockwsk
    library. Measures raw stream throughput of the WSK path, isolating
    the driver from user-mode overhead. Besides AF_VSOCK it can drive
    AF_INET/TCP through the system WSK provider (netio), giving a
    tcpip/virtio-net baseline for the same guest/host pair.

    Configuration is read from the service's Parameters registry subkey
    (all values REG_DWORD unless noted, all optional):
    - Mode      - 0 = receiver (default), 1 = sender.
    - Transport - 0 = AF_VSOCK via viosockwsk (default),
                  1 = TCP via the system WSK provider.
    - PeerCid   - CID the vsock sender connects to (default 2, the host).
    - PeerAddr  - REG_SZ, IPv4 address the TCP sender connects to.
    - LocalAddr - REG_SZ, IPv4 address the TCP endpoint binds to
                  (default INADDR_ANY); use it to pin the traffic to a
                  particular NIC on a multi-homed guest.
    - Port      - port to listen on / connect to (default 1234).
    - BufSize   - size of the transfer buffer in bytes (default 128 KB).
    - BytesMB   - amount of data the sender transmits, in MB (default 1024).
    - Connections - number of concurrent connections (default 1, max 256);
                  connection i uses Port + i, each one has its own thread.

    A receiver thread accepts one connection and drains it until EOF; a
    sender thread pushes BytesMB and disconnects. Per-connection results
    (bytes, elapsed time, Mbit/s) go to WPP at the information level; the
    aggregate is written to the Results subkey of the service key when the
    last thread finishes (Done, Ok, Failed, Status, Bytes, ElapsedUs,
    MbitPerSec, Stalls; Status is the first failed thread's NTSTATUS), so a
    harness can poll it with reg query. The other endpoint is
    anything that speaks a raw byte stream: the Linux vsock_perf, socat,
    etc. */

#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntintsafe.h>
#include <wsk.h>
#include <ip2string.h>
#include <bcrypt.h>
#include "..\..\inc\debug-utils.h"
#include "..\..\inc\vio_wsk.h"
#include "..\..\inc\vio_sockets.h"
#include "..\..\sys\public.h"
#ifdef EVENT_TRACING
#include "viosockwsk-perf.tmh"
#endif

#define VIOWSK_PERF_TAG       (ULONG)'PKSW'

#define DEFAULT_MODE_RECEIVER 0
#define DEFAULT_PEER_CID      2 /* VMADDR_CID_HOST */
#define DEFAULT_PORT          1234
#define DEFAULT_BUF_SIZE      (128 * 1024)
#define DEFAULT_BYTES_MB      1024

#define TRANSPORT_VSOCK       0
#define TRANSPORT_TCP         1

#define DEFAULT_CONNECTIONS   1
#define MAX_CONNECTIONS       256

static ULONG _mode = DEFAULT_MODE_RECEIVER;
static ULONG _transport = TRANSPORT_VSOCK;
static ULONG _peerCid = DEFAULT_PEER_CID;
static ULONG _port = DEFAULT_PORT;
static ULONG _bufSize = DEFAULT_BUF_SIZE;
static ULONG _bytesMB = DEFAULT_BYTES_MB;
static ULONG _connections = DEFAULT_CONNECTIONS;
static IN_ADDR _peerIp;
static WCHAR _peerAddrBuffer[32];
static UNICODE_STRING _peerAddrString = {0, sizeof(_peerAddrBuffer) - sizeof(WCHAR), _peerAddrBuffer};
static IN_ADDR _localIp;
static WCHAR _localAddrBuffer[32];
static UNICODE_STRING _localAddrString = {0, sizeof(_localAddrBuffer) - sizeof(WCHAR), _localAddrBuffer};

typedef union _PERF_SOCKADDR {
    SOCKADDR_VM Vm;
    SOCKADDR_IN In;
} PERF_SOCKADDR;

/* One slot per connection thread; the last thread to finish folds them into
 * the Results registry key. */
typedef struct _PERF_RESULT
{
    NTSTATUS Status;
    ULONGLONG Bytes;
    ULONGLONG ElapsedUs;
    ULONGLONG Stalls;
} PERF_RESULT, *PPERF_RESULT;

static volatile LONG _terminate;
static volatile LONG _running;
static PETHREAD *_workerThreads;
static ULONG _workerCount;
static PPERF_RESULT _results;
static UNICODE_STRING _resultsKeyPath;
static PDEVICE_OBJECT _shutdownDeviceObject = NULL;
static WSK_REGISTRATION _vioWskRegistration;
static WSK_PROVIDER_NPI _vioWskProviderNPI;
// clang-format off
static WSK_CLIENT_NPI _vioWskClientNPI =  {
	NULL,
	NULL,
};
// clang-format on
static const WSK_CLIENT_DISPATCH _sysWskClientDispatch = {MAKE_WSK_VERSION(1, 0), 0, NULL};
static WSK_CLIENT_NPI _sysWskClientNPI = {NULL, &_sysWskClientDispatch};

static NTSTATUS _GeneralIrpComplete(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p; Context=0x%p", DeviceObject, Irp, Context);

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    status = STATUS_MORE_PROCESSING_REQUIRED;

    DEBUG_EXIT_FUNCTION("0x%x", status);
    return status;
}

#define WSK_SYNCHRONOUS_CALL(aIrp, aEvent, aCall, aIosb)                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        IoSetCompletionRoutine((aIrp), _GeneralIrpComplete, (aEvent), TRUE, TRUE, TRUE);                               \
        (aIosb)->Status = (aCall);                                                                                     \
        if ((aIosb)->Status == STATUS_PENDING)                                                                         \
        {                                                                                                              \
            NTSTATUS __waitResult = STATUS_TIMEOUT;                                                                    \
            LARGE_INTEGER __timeout;                                                                                   \
                                                                                                                       \
            __timeout.QuadPart = -10000000;                                                                            \
            do                                                                                                         \
            {                                                                                                          \
                __waitResult = KeWaitForSingleObject((aEvent), Executive, KernelMode, FALSE, &__timeout);              \
                if (__waitResult == STATUS_TIMEOUT && InterlockedCompareExchange(&_terminate, 1, 1))                   \
                {                                                                                                      \
                    IoCancelIrp((aIrp));                                                                               \
                    break;                                                                                             \
                }                                                                                                      \
            } while (__waitResult != STATUS_WAIT_0);                                                                   \
                                                                                                                       \
            KeWaitForSingleObject((aEvent), Executive, KernelMode, FALSE, NULL);                                       \
            (aIosb)->Status = (aIrp)->IoStatus.Status;                                                                 \
        }                                                                                                              \
                                                                                                                       \
        ASSERT((aIosb)->Status == (aIrp)->IoStatus.Status);                                                            \
        (aIosb)->Information = (aIrp)->IoStatus.Information;                                                           \
        KeResetEvent((aEvent));                                                                                        \
        IoReuseIrp((aIrp), STATUS_UNSUCCESSFUL);                                                                       \
    } while (FALSE)

static void _ReportResult(_In_ ULONG Index,
                          _In_ const char *Role,
                          _In_ ULONGLONG Bytes,
                          _In_ LARGE_INTEGER Start,
                          _In_ LARGE_INTEGER End,
                          _In_ NTSTATUS Status,
                          _In_ ULONGLONG Stalls)
{
    LARGE_INTEGER freq;
    ULONGLONG elapsedUs = 0;
    ULONGLONG mbitPerSec = 0;

    KeQueryPerformanceCounter(&freq);
    elapsedUs = (ULONGLONG)(End.QuadPart - Start.QuadPart) * 1000000ULL / (ULONGLONG)freq.QuadPart;
    if (elapsedUs == 0)
    {
        elapsedUs = 1;
    }

    mbitPerSec = Bytes * 8ULL / elapsedUs;
    DEBUG_INFO("PERF %s %u: %I64u bytes in %I64u us -> %I64u Mbit/s, status 0x%x\n",
               Role,
               Index,
               Bytes,
               elapsedUs,
               mbitPerSec,
               Status);
    _results[Index].Status = Status;
    _results[Index].Bytes = Bytes;
    _results[Index].ElapsedUs = elapsedUs;
    _results[Index].Stalls = Stalls;
}

static void _SetResultValue(_In_ HANDLE Key, _In_ PCWSTR Name, _In_ ULONG Type, _In_ PVOID Data, _In_ ULONG Size)
{
    UNICODE_STRING name;
    NTSTATUS status;

    RtlInitUnicodeString(&name, Name);
    status = ZwSetValueKey(Key, &name, 0, Type, Data, Size);
    if (!NT_SUCCESS(status))
    {
        DEBUG_ERROR("ZwSetValueKey(%ws): 0x%x", Name, status);
    }
}

/* Results subkey of the service key: Done flips to 1 last, so a reader that
 * sees it can trust the other values. */
static void _WriteResults(_In_ BOOLEAN Done)
{
    OBJECT_ATTRIBUTES oa;
    HANDLE key = NULL;
    NTSTATUS status;
    ULONG i;
    ULONG ok = 0;
    ULONG failed = 0;
    ULONG done = Done ? 1 : 0;
    NTSTATUS firstFailure = STATUS_SUCCESS;
    ULONGLONG bytes = 0;
    ULONGLONG elapsedUs = 0;
    ULONGLONG stalls = 0;
    ULONGLONG mbitPerSec = 0;

    for (i = 0; i < _workerCount && Done; ++i)
    {
        if (NT_SUCCESS(_results[i].Status))
        {
            ++ok;
        }
        else
        {
            if (failed++ == 0)
            {
                firstFailure = _results[i].Status;
            }
        }
        bytes += _results[i].Bytes;
        stalls += _results[i].Stalls;
        if (_results[i].ElapsedUs > elapsedUs)
        {
            elapsedUs = _results[i].ElapsedUs;
        }
    }
    if (elapsedUs)
    {
        mbitPerSec = bytes * 8ULL / elapsedUs;
    }

    InitializeObjectAttributes(&oa, &_resultsKeyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwCreateKey(&key, KEY_SET_VALUE, &oa, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    if (!NT_SUCCESS(status))
    {
        DEBUG_ERROR("ZwCreateKey(Results): 0x%x", status);
        return;
    }

    _SetResultValue(key, L"Connections", REG_DWORD, &_workerCount, sizeof(_workerCount));
    _SetResultValue(key, L"Ok", REG_DWORD, &ok, sizeof(ok));
    _SetResultValue(key, L"Failed", REG_DWORD, &failed, sizeof(failed));
    _SetResultValue(key, L"Status", REG_DWORD, &firstFailure, sizeof(firstFailure));
    _SetResultValue(key, L"Bytes", REG_QWORD, &bytes, sizeof(bytes));
    _SetResultValue(key, L"ElapsedUs", REG_QWORD, &elapsedUs, sizeof(elapsedUs));
    _SetResultValue(key, L"MbitPerSec", REG_QWORD, &mbitPerSec, sizeof(mbitPerSec));
    _SetResultValue(key, L"Stalls", REG_QWORD, &stalls, sizeof(stalls));
    _SetResultValue(key, L"Done", REG_DWORD, &done, sizeof(done));
    ZwClose(key);
    DEBUG_INFO("PERF results: ok %u, failed %u, %I64u bytes, %I64u Mbit/s aggregate, done %u\n",
               ok,
               failed,
               bytes,
               mbitPerSec,
               done);
}

static NTSTATUS _CloseSocket(_In_ PWSK_SOCKET Socket, _In_ PIRP Irp, _In_ PKEVENT Event)
{
    IO_STATUS_BLOCK iosb;

    memset(&iosb, 0, sizeof(iosb));
    WSK_SYNCHRONOUS_CALL(Irp,
                         Event,
                         ((PWSK_PROVIDER_BASIC_DISPATCH)Socket->Dispatch)->WskCloseSocket(Socket, Irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status))
    {
        DEBUG_ERROR("Unable to close socket: 0x%x", iosb.Status);
    }

    return iosb.Status;
}

static void _SenderLoop(_In_ ULONG Index, _In_ PIRP Irp, _In_ PKEVENT Event, _In_ PWSK_BUF Buffer)
{
    IO_STATUS_BLOCK iosb;
    PERF_SOCKADDR localAddr;
    PERF_SOCKADDR remoteAddr;
    ULONG protocol = 0;
    ULONG port = _port + Index;
    PWSK_SOCKET socket = NULL;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    LARGE_INTEGER retryDelay;
    ULONGLONG total = 0;
    ULONGLONG stalls = 0;
    ULONGLONG target = (ULONGLONG)_bytesMB << 20;
    NTSTATUS result = STATUS_SUCCESS;

    memset(&localAddr, 0, sizeof(localAddr));
    memset(&remoteAddr, 0, sizeof(remoteAddr));
    if (_transport == TRANSPORT_TCP)
    {
        protocol = IPPROTO_TCP;
        localAddr.In.sin_family = AF_INET;
        localAddr.In.sin_addr = _localIp;
        remoteAddr.In.sin_family = AF_INET;
        remoteAddr.In.sin_port = RtlUshortByteSwap((USHORT)port);
        remoteAddr.In.sin_addr = _peerIp;
    }
    else
    {
        localAddr.Vm.svm_family = AF_VSOCK;
        localAddr.Vm.svm_cid = (UINT)VMADDR_CID_ANY;
        localAddr.Vm.svm_port = (UINT)VMADDR_PORT_ANY;
        remoteAddr.Vm.svm_family = AF_VSOCK;
        remoteAddr.Vm.svm_cid = _peerCid;
        remoteAddr.Vm.svm_port = port;
    }

    retryDelay.QuadPart = -10000000;
    iosb.Status = STATUS_CANCELLED; /* unload before the first connect attempt */

    while (!InterlockedCompareExchange(&_terminate, 1, 1))
    {
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             _vioWskProviderNPI.Dispatch->WskSocketConnect(_vioWskProviderNPI.Client,
                                                                           SOCK_STREAM,
                                                                           protocol,
                                                                           (PSOCKADDR)&localAddr,
                                                                           (PSOCKADDR)&remoteAddr,
                                                                           WSK_FLAG_CONNECTION_SOCKET,
                                                                           NULL,
                                                                           NULL,
                                                                           NULL,
                                                                           NULL,
                                                                           NULL,
                                                                           Irp),
                             &iosb);
        if (NT_SUCCESS(iosb.Status))
        {
            socket = (PWSK_SOCKET)iosb.Information;
            break;
        }

        DEBUG_WARNING("Unable to connect, port %u (0x%x), retrying\n", port, iosb.Status);
        KeDelayExecutionThread(KernelMode, FALSE, &retryDelay);
    }

    if (!socket)
    {
        start = KeQueryPerformanceCounter(NULL);
        _ReportResult(Index, "sender", 0, start, start, NT_SUCCESS(iosb.Status) ? STATUS_CANCELLED : iosb.Status, 0);
        return;
    }

    DEBUG_INFO("PERF sender %u: connected (transport %u, port %u), sending %I64u bytes in %u-byte chunks\n",
               Index,
               _transport,
               port,
               target,
               _bufSize);
    start = KeQueryPerformanceCounter(NULL);
    while (total < target && !InterlockedCompareExchange(&_terminate, 1, 1))
    {
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)->WskSend(socket, Buffer, 0, Irp),
                             &iosb);
        if (iosb.Status == STATUS_BUFFER_TOO_SMALL)
        {
            /* Zero peer credit. The driver cannot pend a blocking write on
               credit (the user-mode provider emulates blocking by waiting
               for FD_WRITE and retrying), so retry until the peer reads. */
            ++stalls;
            continue;
        }

        if (!NT_SUCCESS(iosb.Status) || iosb.Information == 0)
        {
            DEBUG_ERROR("Send failed: 0x%x (%I64u bytes transferred)", iosb.Status, (ULONGLONG)iosb.Information);
            result = NT_SUCCESS(iosb.Status) ? STATUS_CONNECTION_DISCONNECTED : iosb.Status;
            break;
        }

        total += iosb.Information;
    }

    end = KeQueryPerformanceCounter(NULL);
    if (NT_SUCCESS(result) && total < target)
    {
        result = STATUS_CANCELLED; /* cut short by unload */
    }
    _ReportResult(Index, "sender", total, start, end, result, stalls);
    WSK_SYNCHRONOUS_CALL(Irp,
                         Event,
                         ((PWSK_PROVIDER_CONNECTION_DISPATCH)socket->Dispatch)->WskDisconnect(socket, NULL, 0, Irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status) && iosb.Status != STATUS_CONNECTION_INVALID)
    {
        DEBUG_ERROR("Unable to disconnect: 0x%x", iosb.Status);
    }

    _CloseSocket(socket, Irp, Event);

    return;
}

static void _ReceiverLoop(_In_ ULONG Index, _In_ PIRP Irp, _In_ PKEVENT Event, _In_ PWSK_BUF Buffer)
{
    IO_STATUS_BLOCK iosb;
    ULONG nonBlocking = 0;
    PERF_SOCKADDR listenAddr;
    ADDRESS_FAMILY family = AF_VSOCK;
    ULONG protocol = 0;
    ULONG port = _port + Index;
    PWSK_SOCKET listenSocket = NULL;
    PWSK_SOCKET clientSocket = NULL;
    LARGE_INTEGER acceptDelay;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    ULONGLONG total = 0;
    NTSTATUS result = STATUS_SUCCESS;

    memset(&listenAddr, 0, sizeof(listenAddr));
    if (_transport == TRANSPORT_TCP)
    {
        family = AF_INET;
        protocol = IPPROTO_TCP;
        listenAddr.In.sin_family = AF_INET;
        listenAddr.In.sin_addr = _localIp;
        listenAddr.In.sin_port = RtlUshortByteSwap((USHORT)port);
    }
    else
    {
        listenAddr.Vm.svm_family = AF_VSOCK;
        listenAddr.Vm.svm_cid = (UINT)VMADDR_CID_ANY;
        listenAddr.Vm.svm_port = port;
    }

    acceptDelay.QuadPart = -10000000;

    WSK_SYNCHRONOUS_CALL(Irp,
                         Event,
                         _vioWskProviderNPI.Dispatch->WskSocket(_vioWskProviderNPI.Client,
                                                                family,
                                                                SOCK_STREAM,
                                                                protocol,
                                                                WSK_FLAG_LISTEN_SOCKET,
                                                                NULL,
                                                                NULL,
                                                                NULL,
                                                                NULL,
                                                                NULL,
                                                                Irp),
                         &iosb);
    start = KeQueryPerformanceCounter(NULL);
    if (!NT_SUCCESS(iosb.Status))
    {
        DEBUG_ERROR("Unable to create listen socket: 0x%x", iosb.Status);
        _ReportResult(Index, "receiver", 0, start, start, iosb.Status, 0);
        return;
    }

    listenSocket = (PWSK_SOCKET)iosb.Information;
    WSK_SYNCHRONOUS_CALL(Irp,
                         Event,
                         ((PWSK_PROVIDER_LISTEN_DISPATCH)listenSocket->Dispatch)->WskBind(listenSocket, (PSOCKADDR)&listenAddr, 0, Irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status))
    {
        DEBUG_ERROR("Unable to bind to port %u: 0x%x", port, iosb.Status);
        result = iosb.Status;
        goto CloseListenSocket;
    }

    /* Non-blocking accept for vsock only: the socket driver does not support
       accept cancellation, so poll with a 1s sleep to allow driver unload.
       The system WSK provider cancels pended accepts just fine. */
    if (_transport == TRANSPORT_VSOCK)
    {
        nonBlocking = TRUE;
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_LISTEN_DISPATCH)listenSocket->Dispatch)
                                                                                                                                 ->WskControlSocket(listenSocket,
                                                                                                                                                    WskIoctl,
                                                                                                                                                    FIONBIO,
                                                                                                                                                    0,
                                                                                                                                                    sizeof(nonBlocking),
                                                                                                                                                    &nonBlocking,
                                                                                                                                                    0,
                                                                                                                                                    NULL,
                                                                                                                                                    NULL,
                                                                                                                                                    Irp),
                             &iosb);
        if (!NT_SUCCESS(iosb.Status))
        {
            DEBUG_ERROR("Unable to make the listen socket non-blocking: 0x%x", iosb.Status);
            result = iosb.Status;
            goto CloseListenSocket;
        }
    }

    DEBUG_INFO("PERF receiver %u: listening on port %u, buffer %u bytes\n", Index, port, _bufSize);
    nonBlocking = FALSE;
    /* One connection per thread: poll accept until it arrives or unload. */
    iosb.Status = STATUS_CANT_WAIT;
    while (!InterlockedCompareExchange(&_terminate, 1, 1))
    {
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_LISTEN_DISPATCH)listenSocket->Dispatch)->WskAccept(listenSocket, WSK_FLAG_CONNECTION_SOCKET, NULL, NULL, NULL, NULL, Irp),
                             &iosb);
        if (iosb.Status != STATUS_CANT_WAIT)
        {
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &acceptDelay);
    }

    if (!NT_SUCCESS(iosb.Status) || iosb.Status == STATUS_CANT_WAIT)
    {
        /* STATUS_CANCELLED / CANT_WAIT here: unload got there first. */
        DEBUG_ERROR("Unable to accept: 0x%x", iosb.Status);
        result = (iosb.Status == STATUS_CANT_WAIT) ? STATUS_CANCELLED : iosb.Status;
        goto CloseListenSocket;
    }

    clientSocket = (PWSK_SOCKET)iosb.Information;
    if (_transport == TRANSPORT_VSOCK)
    {
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_LISTEN_DISPATCH)clientSocket->Dispatch)
                                                                                                                                 ->WskControlSocket(clientSocket,
                                                                                                                                                    WskIoctl,
                                                                                                                                                    FIONBIO,
                                                                                                                                                    0,
                                                                                                                                                    sizeof(nonBlocking),
                                                                                                                                                    &nonBlocking,
                                                                                                                                                    0,
                                                                                                                                                    NULL,
                                                                                                                                                    NULL,
                                                                                                                                                    Irp),
                             &iosb);
        if (!NT_SUCCESS(iosb.Status))
        {
            DEBUG_ERROR("Unable to make the accepted socket blocking: 0x%x", iosb.Status);
            result = iosb.Status;
            goto CloseClientSocket;
        }
    }

    start = KeQueryPerformanceCounter(NULL);
    while (!InterlockedCompareExchange(&_terminate, 1, 1))
    {
        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_CONNECTION_DISPATCH)clientSocket->Dispatch)->WskReceive(clientSocket, Buffer, 0, Irp),
                             &iosb);
        if (!NT_SUCCESS(iosb.Status))
        {
            DEBUG_WARNING("Receive failed: 0x%x (%I64u bytes so far)\n", iosb.Status, total);
            result = iosb.Status;
            break;
        }

        if (iosb.Information == 0)
        {
            result = STATUS_SUCCESS; /* EOF - the peer closed the connection */
            goto CloseClientSocket;
        }

        total += iosb.Information;
    }
    if (NT_SUCCESS(result))
    {
        result = STATUS_CANCELLED; /* no EOF: cut short by unload */
    }

CloseClientSocket:
    _CloseSocket(clientSocket, Irp, Event);

CloseListenSocket:
    _CloseSocket(listenSocket, Irp, Event);
    end = KeQueryPerformanceCounter(NULL);
    _ReportResult(Index, "receiver", total, start, end, result, 0);

    return;
}

static void _PerfThreadRoutine(_In_opt_ PVOID Context)
{
    ULONG index = (ULONG)(ULONG_PTR)Context;
    KEVENT event;
    PIRP irp = NULL;
    PMDL mdl = NULL;
    PVOID buffer = NULL;
    WSK_BUF wskBuf;
    DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

    _results[index].Status = STATUS_INSUFFICIENT_RESOURCES;
    buffer = ExAllocatePoolUninitialized(NonPagedPool, _bufSize, VIOWSK_PERF_TAG);
    if (!buffer)
    {
        DEBUG_ERROR("Unable to allocate the transfer buffer");
        goto Exit;
    }

    memset(buffer, 0xA5, _bufSize);
    mdl = IoAllocateMdl(buffer, _bufSize, FALSE, FALSE, NULL);
    if (!mdl)
    {
        DEBUG_ERROR("Unable to allocate the transfer MDL");
        goto FreeBuffer;
    }

    MmBuildMdlForNonPagedPool(mdl);
    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = _bufSize;
    irp = IoAllocateIrp(1, FALSE);
    if (!irp)
    {
        DEBUG_ERROR("Unable to allocate IRP");
        goto FreeMdl;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    if (_mode == DEFAULT_MODE_RECEIVER)
    {
        _ReceiverLoop(index, irp, &event, &wskBuf);
    }
    else
    {
        _SenderLoop(index, irp, &event, &wskBuf);
    }

    IoFreeIrp(irp);
FreeMdl:
    IoFreeMdl(mdl);
FreeBuffer:
    ExFreePoolWithTag(buffer, VIOWSK_PERF_TAG);
Exit:
    if (InterlockedDecrement(&_running) == 0)
    {
        _WriteResults(TRUE);
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

static void _StopWorkers(void)
{
    ULONG i;

    InterlockedExchange(&_terminate, 1);
    for (i = 0; i < _workerCount; ++i)
    {
        if (_workerThreads[i])
        {
            KeWaitForSingleObject(_workerThreads[i], Executive, KernelMode, FALSE, NULL);
            ObDereferenceObject(_workerThreads[i]);
            _workerThreads[i] = NULL;
        }
    }
}

static NTSTATUS _StartWorkers(void)
{
    NTSTATUS status = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES oa;
    CLIENT_ID clientId;
    HANDLE hThread = NULL;
    ULONG i;

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    _running = (LONG)_workerCount;
    for (i = 0; i < _workerCount; ++i)
    {
        status = PsCreateSystemThread(&hThread,
                                      SYNCHRONIZE,
                                      &oa,
                                      NULL,
                                      &clientId,
                                      _PerfThreadRoutine,
                                      (PVOID)(ULONG_PTR)i);
        if (!NT_SUCCESS(status))
        {
            /* Threads never started will not decrement _running. */
            InterlockedAdd(&_running, -(LONG)(_workerCount - i));
            _StopWorkers();
            return status;
        }

        status = ObReferenceObjectByHandle(hThread, SYNCHRONIZE, *PsThreadType, KernelMode, &_workerThreads[i], NULL);
        ZwClose(hThread);
        if (!NT_SUCCESS(status))
        {
            InterlockedAdd(&_running, -(LONG)(_workerCount - i - 1));
            _StopWorkers();
            return status;
        }
    }

    return STATUS_SUCCESS;
}

static void _LoadRegistryParameters(_In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PWCHAR ParamsPath = NULL;
    SIZE_T ParamsPathSize = 0;
    RTL_QUERY_REGISTRY_TABLE Table[10];
    DEBUG_ENTER_FUNCTION("RegistryPath=\"%wZ\"", RegistryPath);

    ParamsPathSize = RegistryPath->Length + sizeof(L"\\Parameters");
    ParamsPath = ExAllocatePoolUninitialized(PagedPool, ParamsPathSize, VIOWSK_PERF_TAG);
    if (!ParamsPath)
    {
        goto Exit;
    }

    RtlCopyMemory(ParamsPath, RegistryPath->Buffer, RegistryPath->Length);
    RtlCopyMemory((PUCHAR)ParamsPath + RegistryPath->Length, L"\\Parameters", sizeof(L"\\Parameters"));
    memset(Table, 0, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = L"Mode";
    Table[0].EntryContext = &_mode;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[1].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[1].Name = L"PeerCid";
    Table[1].EntryContext = &_peerCid;
    Table[1].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[2].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[2].Name = L"Port";
    Table[2].EntryContext = &_port;
    Table[2].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[3].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[3].Name = L"BufSize";
    Table[3].EntryContext = &_bufSize;
    Table[3].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[4].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[4].Name = L"BytesMB";
    Table[4].EntryContext = &_bytesMB;
    Table[4].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[5].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[5].Name = L"Transport";
    Table[5].EntryContext = &_transport;
    Table[5].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[6].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Table[6].Name = L"PeerAddr";
    Table[6].EntryContext = &_peerAddrString;
    Table[7].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Table[7].Name = L"LocalAddr";
    Table[7].EntryContext = &_localAddrString;
    Table[8].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[8].Name = L"Connections";
    Table[8].EntryContext = &_connections;
    Table[8].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, ParamsPath, Table, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("RtlQueryRegistryValues: 0x%x, using defaults", Status);
    }

    if (_bufSize == 0 || _bufSize > (64 * 1024 * 1024))
    {
        _bufSize = DEFAULT_BUF_SIZE;
    }

    if (_connections == 0 || _connections > MAX_CONNECTIONS)
    {
        _connections = (_connections == 0) ? DEFAULT_CONNECTIONS : MAX_CONNECTIONS;
    }

    if (_peerAddrString.Length > 0)
    {
        PCWSTR terminator = NULL;

        _peerAddrBuffer[_peerAddrString.Length / sizeof(WCHAR)] = L'\0';
        Status = RtlIpv4StringToAddressW(_peerAddrBuffer, TRUE, &terminator, &_peerIp);
        if (!NT_SUCCESS(Status))
        {
            DEBUG_ERROR("Invalid PeerAddr, expected an IPv4 address: 0x%x", Status);
        }
    }
    else if (_transport == TRANSPORT_TCP && _mode != DEFAULT_MODE_RECEIVER)
    {
        DEBUG_ERROR("The TCP sender requires PeerAddr");
    }

    if (_localAddrString.Length > 0)
    {
        PCWSTR terminator = NULL;

        _localAddrBuffer[_localAddrString.Length / sizeof(WCHAR)] = L'\0';
        Status = RtlIpv4StringToAddressW(_localAddrBuffer, TRUE, &terminator, &_localIp);
        if (!NT_SUCCESS(Status))
        {
            DEBUG_ERROR("Invalid LocalAddr, expected an IPv4 address: 0x%x", Status);
        }
    }

    ExFreePoolWithTag(ParamsPath, VIOWSK_PERF_TAG);
Exit:
    DEBUG_EXIT_FUNCTION("void, _mode=%u, _transport=%u, _peerCid=%u, _port=%u, _bufSize=%u, _bytesMB=%u, "
                        "_connections=%u",
                        _mode,
                        _transport,
                        _peerCid,
                        _port,
                        _bufSize,
                        _bytesMB,
                        _connections);
    return;
}

static NTSTATUS _WskAttach(_In_ PDRIVER_OBJECT DriverObject,
                           _In_ PUNICODE_STRING RegistryPath,
                           _In_ PDEVICE_OBJECT Device)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (_transport == TRANSPORT_TCP)
    {
        Status = WskRegister(&_sysWskClientNPI, &_vioWskRegistration);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Status = WskCaptureProviderNPI(&_vioWskRegistration, WSK_INFINITE_WAIT, &_vioWskProviderNPI);
        if (!NT_SUCCESS(Status))
        {
            WskDeregister(&_vioWskRegistration);
        }

        return Status;
    }

    Status = VioWskModuleInit(DriverObject, RegistryPath, Device);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = VioWskRegister(&_vioWskClientNPI, &_vioWskRegistration);
    if (!NT_SUCCESS(Status))
    {
        VioWskModuleFinit();
        return Status;
    }

    Status = VioWskCaptureProviderNPI(&_vioWskRegistration, WSK_INFINITE_WAIT, &_vioWskProviderNPI);
    if (!NT_SUCCESS(Status))
    {
        VioWskDeregister(&_vioWskRegistration);
        VioWskModuleFinit();
    }

    return Status;
}

static void _WskDetach(void)
{
    if (_transport == TRANSPORT_TCP)
    {
        WskReleaseProviderNPI(&_vioWskRegistration);
        WskDeregister(&_vioWskRegistration);
        return;
    }

    VioWskReleaseProviderNPI(&_vioWskRegistration);
    VioWskDeregister(&_vioWskRegistration);
    VioWskModuleFinit();
}

static void DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    DEBUG_ENTER_FUNCTION("DriverObject=0x%p", DriverObject);

    UNREFERENCED_PARAMETER(DriverObject);

    _StopWorkers();
    _WskDetach();
    IoDeleteDevice(_shutdownDeviceObject);
    ExFreePoolWithTag(_workerThreads, VIOWSK_PERF_TAG);
    ExFreePoolWithTag(_results, VIOWSK_PERF_TAG);
    ExFreePoolWithTag(_resultsKeyPath.Buffer, VIOWSK_PERF_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    WPP_CLEANUP(DriverObject);
    return;
}

/* Results key path = <service key>\Results, kept for the run (RegistryPath
 * is only valid during DriverEntry); worker and result arrays sized by
 * Connections. */
static NTSTATUS _AllocateRunState(_In_ PUNICODE_STRING RegistryPath)
{
    static const WCHAR suffix[] = L"\\Results";
    USHORT length = (USHORT)(RegistryPath->Length + sizeof(suffix) - sizeof(WCHAR));

    _resultsKeyPath.Buffer = ExAllocatePoolZero(PagedPool, length, VIOWSK_PERF_TAG);
    _workerThreads = ExAllocatePoolZero(NonPagedPool, _connections * sizeof(PETHREAD), VIOWSK_PERF_TAG);
    _results = ExAllocatePoolZero(NonPagedPool, _connections * sizeof(PERF_RESULT), VIOWSK_PERF_TAG);
    if (!_resultsKeyPath.Buffer || !_workerThreads || !_results)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(_resultsKeyPath.Buffer, RegistryPath->Buffer, RegistryPath->Length);
    RtlCopyMemory((PUCHAR)_resultsKeyPath.Buffer + RegistryPath->Length, suffix, sizeof(suffix) - sizeof(WCHAR));
    _resultsKeyPath.Length = length;
    _resultsKeyPath.MaximumLength = length;
    _workerCount = _connections;

    return STATUS_SUCCESS;
}

static void _FreeRunState(void)
{
    if (_workerThreads)
    {
        ExFreePoolWithTag(_workerThreads, VIOWSK_PERF_TAG);
    }
    if (_results)
    {
        ExFreePoolWithTag(_results, VIOWSK_PERF_TAG);
    }
    if (_resultsKeyPath.Buffer)
    {
        ExFreePoolWithTag(_resultsKeyPath.Buffer, VIOWSK_PERF_TAG);
    }
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT Device = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    DEBUG_ENTER_FUNCTION("DriverObject=0x%p; RegistryPath=\"%wZ\"", DriverObject, RegistryPath);

    _LoadRegistryParameters(RegistryPath);
    Status = _AllocateRunState(RegistryPath);
    if (!NT_SUCCESS(Status))
    {
        goto FreeRunState;
    }

    /* Done=0 before the first thread runs, so a stale result is never read. */
    _WriteResults(FALSE);
    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Device);
    if (!NT_SUCCESS(Status))
    {
        goto FreeRunState;
    }

    Status = _WskAttach(DriverObject, RegistryPath, Device);
    if (!NT_SUCCESS(Status))
    {
        goto DeleteDevice;
    }

    Status = _StartWorkers();
    if (!NT_SUCCESS(Status))
    {
        goto WskDetach;
    }

    DriverObject->DriverUnload = DriverUnload;
    _shutdownDeviceObject = (PDEVICE_OBJECT)InterlockedExchangePointer(&Device, NULL);

WskDetach:
    if (!NT_SUCCESS(Status))
    {
        _WskDetach();
    }
DeleteDevice:
    if (Device)
    {
        IoDeleteDevice(Device);
    }
FreeRunState:
    if (!NT_SUCCESS(Status))
    {
        _FreeRunState();
        WPP_CLEANUP(DriverObject);
    }

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}
