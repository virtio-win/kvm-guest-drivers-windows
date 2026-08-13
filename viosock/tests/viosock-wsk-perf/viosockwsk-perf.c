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

    The receiver accepts connections in a loop and drains each one until
    EOF; the sender pushes BytesMB and disconnects. Results (bytes,
    elapsed time, Mbit/s) are reported via WPP at the information level.
    The other endpoint is anything that speaks a raw byte stream: the
    Linux vsock_perf, socat, etc. */

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

static ULONG _mode = DEFAULT_MODE_RECEIVER;
static ULONG _transport = TRANSPORT_VSOCK;
static ULONG _peerCid = DEFAULT_PEER_CID;
static ULONG _port = DEFAULT_PORT;
static ULONG _bufSize = DEFAULT_BUF_SIZE;
static ULONG _bytesMB = DEFAULT_BYTES_MB;
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

static volatile LONG _terminate;
static PETHREAD _workerThread;
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

static void _ReportResult(_In_ const char *Role, _In_ ULONGLONG Bytes, _In_ LARGE_INTEGER Start, _In_ LARGE_INTEGER End)
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
    DEBUG_INFO("PERF %s: %I64u bytes in %I64u us -> %I64u Mbit/s\n", Role, Bytes, elapsedUs, mbitPerSec);
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

static void _SenderLoop(_In_ PIRP Irp, _In_ PKEVENT Event, _In_ PWSK_BUF Buffer)
{
    IO_STATUS_BLOCK iosb;
    PERF_SOCKADDR localAddr;
    PERF_SOCKADDR remoteAddr;
    ULONG protocol = 0;
    PWSK_SOCKET socket = NULL;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    LARGE_INTEGER retryDelay;
    ULONGLONG total = 0;
    ULONGLONG stalls = 0;
    ULONGLONG target = (ULONGLONG)_bytesMB << 20;

    memset(&localAddr, 0, sizeof(localAddr));
    memset(&remoteAddr, 0, sizeof(remoteAddr));
    if (_transport == TRANSPORT_TCP)
    {
        protocol = IPPROTO_TCP;
        localAddr.In.sin_family = AF_INET;
        localAddr.In.sin_addr = _localIp;
        remoteAddr.In.sin_family = AF_INET;
        remoteAddr.In.sin_port = RtlUshortByteSwap((USHORT)_port);
        remoteAddr.In.sin_addr = _peerIp;
    }
    else
    {
        localAddr.Vm.svm_family = AF_VSOCK;
        localAddr.Vm.svm_cid = (UINT)VMADDR_CID_ANY;
        localAddr.Vm.svm_port = (UINT)VMADDR_PORT_ANY;
        remoteAddr.Vm.svm_family = AF_VSOCK;
        remoteAddr.Vm.svm_cid = _peerCid;
        remoteAddr.Vm.svm_port = _port;
    }

    retryDelay.QuadPart = -10000000;

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

        DEBUG_WARNING("Unable to connect, port %u (0x%x), retrying\n", _port, iosb.Status);
        KeDelayExecutionThread(KernelMode, FALSE, &retryDelay);
    }

    if (!socket)
    {
        return;
    }

    DEBUG_INFO("PERF sender: connected (transport %u, port %u), sending %I64u bytes in %u-byte chunks\n",
               _transport,
               _port,
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
            break;
        }

        total += iosb.Information;
    }

    end = KeQueryPerformanceCounter(NULL);
    DEBUG_INFO("PERF sender: %I64u zero-credit stalls\n", stalls);
    _ReportResult("sender", total, start, end);
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

static void _ReceiverLoop(_In_ PIRP Irp, _In_ PKEVENT Event, _In_ PWSK_BUF Buffer)
{
    IO_STATUS_BLOCK iosb;
    ULONG nonBlocking = 0;
    PERF_SOCKADDR listenAddr;
    ADDRESS_FAMILY family = AF_VSOCK;
    ULONG protocol = 0;
    PWSK_SOCKET listenSocket = NULL;
    LARGE_INTEGER acceptDelay;

    memset(&listenAddr, 0, sizeof(listenAddr));
    if (_transport == TRANSPORT_TCP)
    {
        family = AF_INET;
        protocol = IPPROTO_TCP;
        listenAddr.In.sin_family = AF_INET;
        listenAddr.In.sin_addr = _localIp;
        listenAddr.In.sin_port = RtlUshortByteSwap((USHORT)_port);
    }
    else
    {
        listenAddr.Vm.svm_family = AF_VSOCK;
        listenAddr.Vm.svm_cid = (UINT)VMADDR_CID_ANY;
        listenAddr.Vm.svm_port = _port;
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
    if (!NT_SUCCESS(iosb.Status))
    {
        DEBUG_ERROR("Unable to create listen socket: 0x%x", iosb.Status);
        return;
    }

    listenSocket = (PWSK_SOCKET)iosb.Information;
    WSK_SYNCHRONOUS_CALL(Irp,
                         Event,
                         ((PWSK_PROVIDER_LISTEN_DISPATCH)listenSocket->Dispatch)->WskBind(listenSocket, (PSOCKADDR)&listenAddr, 0, Irp),
                         &iosb);
    if (!NT_SUCCESS(iosb.Status))
    {
        DEBUG_ERROR("Unable to bind to port %u: 0x%x", _port, iosb.Status);
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
            goto CloseListenSocket;
        }
    }

    DEBUG_INFO("PERF receiver: listening on port %u, buffer %u bytes\n", _port, _bufSize);
    nonBlocking = FALSE;
    while (!InterlockedCompareExchange(&_terminate, 1, 1))
    {
        PWSK_SOCKET clientSocket = NULL;
        LARGE_INTEGER start;
        LARGE_INTEGER end;
        ULONGLONG total = 0;

        WSK_SYNCHRONOUS_CALL(Irp,
                             Event,
                             ((PWSK_PROVIDER_LISTEN_DISPATCH)listenSocket->Dispatch)->WskAccept(listenSocket, WSK_FLAG_CONNECTION_SOCKET, NULL, NULL, NULL, NULL, Irp),
                             &iosb);
        if (iosb.Status == STATUS_CANT_WAIT)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &acceptDelay);
            continue;
        }

        if (iosb.Status == STATUS_CANCELLED)
        {
            break; /* driver unload cancelled the pended accept */
        }

        if (!NT_SUCCESS(iosb.Status))
        {
            DEBUG_ERROR("Unable to accept: 0x%x", iosb.Status);
            break;
        }

        clientSocket = (PWSK_SOCKET)iosb.Information;
        if (_transport == TRANSPORT_VSOCK)
        {
            WSK_SYNCHRONOUS_CALL(Irp,
                                 Event,
                                 ((PWSK_PROVIDER_LISTEN_DISPATCH)clientSocket->Dispatch)->WskControlSocket(clientSocket, WskIoctl, FIONBIO, 0, sizeof(nonBlocking), &nonBlocking, 0, NULL, NULL, Irp),
                                 &iosb);
            if (!NT_SUCCESS(iosb.Status))
            {
                DEBUG_ERROR("Unable to make the accepted socket blocking: 0x%x", iosb.Status);
                _CloseSocket(clientSocket, Irp, Event);
                continue;
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
                break;
            }

            if (iosb.Information == 0)
            {
                break; /* EOF - the peer closed the connection */
            }

            total += iosb.Information;
        }

        end = KeQueryPerformanceCounter(NULL);
        _ReportResult("receiver", total, start, end);
        _CloseSocket(clientSocket, Irp, Event);
    }

CloseListenSocket:
    _CloseSocket(listenSocket, Irp, Event);

    return;
}

static void _PerfThreadRoutine(_In_opt_ PVOID Context)
{
    KEVENT event;
    PIRP irp = NULL;
    PMDL mdl = NULL;
    PVOID buffer = NULL;
    WSK_BUF wskBuf;
    DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

    UNREFERENCED_PARAMETER(Context);

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
        _ReceiverLoop(irp, &event, &wskBuf);
    }
    else
    {
        _SenderLoop(irp, &event, &wskBuf);
    }

    IoFreeIrp(irp);
FreeMdl:
    IoFreeMdl(mdl);
FreeBuffer:
    ExFreePoolWithTag(buffer, VIOWSK_PERF_TAG);
Exit:
    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

static void _LoadRegistryParameters(_In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PWCHAR ParamsPath = NULL;
    SIZE_T ParamsPathSize = 0;
    RTL_QUERY_REGISTRY_TABLE Table[9];
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
    Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, ParamsPath, Table, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("RtlQueryRegistryValues: 0x%x, using defaults", Status);
    }

    if (_bufSize == 0 || _bufSize > (64 * 1024 * 1024))
    {
        _bufSize = DEFAULT_BUF_SIZE;
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
    DEBUG_EXIT_FUNCTION("void, _mode=%u, _transport=%u, _peerCid=%u, _port=%u, _bufSize=%u, _bytesMB=%u",
                        _mode,
                        _transport,
                        _peerCid,
                        _port,
                        _bufSize,
                        _bytesMB);
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

    InterlockedExchange(&_terminate, 1);
    KeWaitForSingleObject(_workerThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(_workerThread);
    _WskDetach();
    IoDeleteDevice(_shutdownDeviceObject);

    DEBUG_EXIT_FUNCTION_VOID();
    WPP_CLEANUP(DriverObject);
    return;
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    CLIENT_ID clientId;
    OBJECT_ATTRIBUTES oa;
    HANDLE hThread = NULL;
    PDEVICE_OBJECT Device = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    DEBUG_ENTER_FUNCTION("DriverObject=0x%p; RegistryPath=\"%wZ\"", DriverObject, RegistryPath);

    _LoadRegistryParameters(RegistryPath);
    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Device);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = _WskAttach(DriverObject, RegistryPath, Device);
    if (!NT_SUCCESS(Status))
    {
        goto DeleteDevice;
    }

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&hThread, SYNCHRONIZE, &oa, NULL, &clientId, _PerfThreadRoutine, NULL);
    if (!NT_SUCCESS(Status))
    {
        goto WskDetach;
    }

    Status = ObReferenceObjectByHandle(hThread, SYNCHRONIZE, *PsThreadType, KernelMode, &_workerThread, NULL);
    ZwClose(hThread);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&_terminate, 1);
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
Exit:
    if (!NT_SUCCESS(Status))
    {
        WPP_CLEANUP(DriverObject);
    }

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}
