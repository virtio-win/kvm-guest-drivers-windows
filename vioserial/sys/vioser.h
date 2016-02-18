/**********************************************************************
 * Copyright (c) 2010-2016 Red Hat, Inc.
 *
 * File: vioser.h
 *
 * Author(s):
 *
 * Main include file
 * This file contains various routines and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#if !defined(VIOSERIAL_H)
#define VIOSERIAL_H
#include "public.h"

EVT_WDF_DRIVER_DEVICE_ADD VIOSerialEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR                           VIOSerialInterruptIsr;
EVT_WDF_INTERRUPT_DPC                           VIOSerialInterruptDpc;
EVT_WDF_INTERRUPT_DPC                           VIOSerialQueuesInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                        VIOSerialInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                       VIOSerialInterruptDisable;



#define VIRTIO_SERIAL_MAX_PORTS 31
#define VIRTIO_SERIAL_MAX_QUEUES_COUPLES (VIRTIO_SERIAL_MAX_PORTS + 1)
#define VIRTIO_SERIAL_CONTROL_PORT_INDEX 1

#define VIRTIO_SERIAL_INVALID_INTERRUPT_STATUS 0xFF

#define VIRTIO_CONSOLE_F_SIZE      0
#define VIRTIO_CONSOLE_F_MULTIPORT 1
#define VIRTIO_CONSOLE_BAD_ID      (~(u32)0)


#define VIRTIO_CONSOLE_DEVICE_READY     0
#define VIRTIO_CONSOLE_PORT_ADD         1
#define VIRTIO_CONSOLE_PORT_REMOVE      2
#define VIRTIO_CONSOLE_PORT_READY       3

#define VIRTIO_CONSOLE_CONSOLE_PORT     4
#define VIRTIO_CONSOLE_RESIZE           5
#define VIRTIO_CONSOLE_PORT_OPEN        6
#define VIRTIO_CONSOLE_PORT_NAME        7

#define RETRY_THRESHOLD                 400

#pragma pack (push)
#pragma pack (1)

typedef struct _tagConsoleConfig {
    //* colums of the screens
    u16 cols;
    //* rows of the screens
    u16 rows;
    //* max. number of ports this device can hold
    u32 max_nr_ports;
} CONSOLE_CONFIG, * PCONSOLE_CONFIG;
#pragma pack (pop)


#pragma pack (push)
#pragma pack (1)
typedef struct _tagVirtioConsoleControl {
    u32 id;
    u16 event;
    u16 value;
}VIRTIO_CONSOLE_CONTROL, * PVIRTIO_CONSOLE_CONTROL;
#pragma pack (pop)


typedef struct _tagPortDevice
{
    VirtIODevice        *pIODevice;

    PHYSICAL_ADDRESS    PortBasePA;
    ULONG               uPortLength;
    PVOID               pPortBase;
    bool                bPortMapped;

    WDFINTERRUPT        WdfInterrupt;
    WDFINTERRUPT        QueuesInterrupt;

    int                 isHostMultiport;

    CONSOLE_CONFIG      consoleConfig;
    struct virtqueue    *c_ivq, *c_ovq;
    struct virtqueue    **in_vqs, **out_vqs;
    WDFSPINLOCK         CVqLock;

    BOOLEAN             DeviceOK;
    UINT                DeviceId;
} PORTS_DEVICE, *PPORTS_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PORTS_DEVICE, GetPortsDevice)

#define VIOSERIAL_DRIVER_MEMORY_TAG (ULONG)'rsIV'

#define  PORT_DEVICE_ID L"{6FDE7547-1B65-48ae-B628-80BE62016026}\\VIOSerialPort\0"

DEFINE_GUID(GUID_DEVCLASS_PORT_DEVICE,
0x6fde7547, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7547-1B65-48ae-B628-80BE62016026}



#define DEVICE_DESC_LENGTH  128

typedef struct _tagPortBuffer
{
    PHYSICAL_ADDRESS    pa_buf;
    PVOID               va_buf;
    size_t              size;
    size_t              len;
    size_t              offset;
} PORT_BUFFER, * PPORT_BUFFER;

typedef struct _WriteBufferEntry
{
    SINGLE_LIST_ENTRY ListEntry;
    WDFMEMORY EntryHandle;
    WDFREQUEST Request;
    PVOID Buffer;
} WRITE_BUFFER_ENTRY, *PWRITE_BUFFER_ENTRY;

typedef struct _tagVioSerialPort
{
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;

    WDFDEVICE           BusDevice;
    WDFDEVICE           Device;

    PPORT_BUFFER        InBuf;
    WDFSPINLOCK         InBufLock;
    WDFSPINLOCK         OutVqLock;
    ANSI_STRING         NameString;
    UINT                PortId;
    UINT                DeviceId;
    BOOLEAN             OutVqFull;
    BOOLEAN             HostConnected;
    BOOLEAN             GuestConnected;

    BOOLEAN             Removed;
    WDFQUEUE            ReadQueue;
    WDFREQUEST          PendingReadRequest;

    // Hold a list of allocated buffers which were written to the virt queue
    // and was not returned yet.
    SINGLE_LIST_ENTRY   WriteBuffersList;

    WDFQUEUE            WriteQueue;
    WDFQUEUE            IoctlQueue;
} VIOSERIAL_PORT, *PVIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSERIAL_PORT, SerialPortGetData)


typedef struct _tagRawPdoVioSerialPort
{
    PVIOSERIAL_PORT port;
} RAWPDO_VIOSERIAL_PORT, *PRAWPDO_VIOSERIAL_PORT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RAWPDO_VIOSERIAL_PORT, RawPdoSerialPortGetData)


typedef struct _tagDriverContext
{
    // one global lookaside owned by the driver object
    WDFLOOKASIDE WriteBufferLookaside;
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)


NTSTATUS
VIOSerialFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock
);

NTSTATUS
VIOSerialAddInBuf(
    IN struct virtqueue *vq,
    IN PPORT_BUFFER buf
);

VOID
VIOSerialProcessInputBuffers(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialReclaimConsumedBuffers(
    IN PVIOSERIAL_PORT port
);

size_t
VIOSerialSendBuffers(
    IN PVIOSERIAL_PORT Port,
    IN PWRITE_BUFFER_ENTRY Entry,
    IN size_t Length
);

SSIZE_T
VIOSerialFillReadBufLocked(
    IN PVIOSERIAL_PORT port,
    IN PVOID outbuf,
    IN SIZE_T count
);

PPORT_BUFFER
VIOSerialAllocateBuffer(
    IN size_t buf_size
);

VOID
VIOSerialFreeBuffer(
    IN PPORT_BUFFER buf
);

VOID
VIOSerialSendCtrlMsg(
    IN WDFDEVICE hDevice,
    IN ULONG id,
    IN USHORT event,
    IN USHORT value
);

VOID
VIOSerialCtrlWorkHandler(
    IN WDFDEVICE Device
);

PVIOSERIAL_PORT
VIOSerialFindPortById(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
VIOSerialAddPort(
    IN WDFDEVICE Device,
    IN ULONG id
);

VOID
VIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN PVIOSERIAL_PORT port
);

VOID
VIOSerialInitPortConsole(
    IN PVIOSERIAL_PORT port
);

VOID
VIOSerialDiscardPortDataLocked(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialPortHasDataLocked(
    IN PVIOSERIAL_PORT port
);

PVOID
VIOSerialGetInBuf(
    IN PVIOSERIAL_PORT port
);

BOOLEAN
VIOSerialWillWriteBlock(
    IN PVIOSERIAL_PORT port
);

VOID
VIOSerialEnableInterruptQueue(IN struct virtqueue *vq);

VOID
VIOSerialDisableInterruptQueue(IN struct virtqueue *vq);

EVT_WDF_CHILD_LIST_CREATE_DEVICE VIOSerialDeviceListCreatePdo;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE VIOSerialEvtChildListIdentificationDescriptionCompare;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_CLEANUP VIOSerialEvtChildListIdentificationDescriptionCleanup;
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_DUPLICATE VIOSerialEvtChildListIdentificationDescriptionDuplicate;
EVT_WDF_IO_QUEUE_IO_READ VIOSerialPortRead;
EVT_WDF_IO_QUEUE_IO_WRITE VIOSerialPortWrite;
EVT_WDF_IO_QUEUE_IO_STOP VIOSerialPortReadIoStop;
EVT_WDF_IO_QUEUE_IO_STOP VIOSerialPortWriteIoStop;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VIOSerialPortDeviceControl;
EVT_WDF_DEVICE_FILE_CREATE VIOSerialPortCreate;
EVT_WDF_FILE_CLOSE VIOSerialPortClose;

VOID
VIOSerialPortCreateName (
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN PPORT_BUFFER buf
);

VOID
VIOSerialPortPnpNotify (
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN BOOLEAN connected
);

VOID
VIOSerialPortCreateSymbolicName(
    IN WDFWORKITEM  WorkItem
);

__inline
struct
virtqueue*
GetInQueue (
    IN PVIOSERIAL_PORT port
)
{
    PPORTS_DEVICE    pContext = NULL;
    ASSERT (port);
    ASSERT (port->BusDevice);
    pContext = GetPortsDevice(port->BusDevice);
    ASSERT (pContext->in_vqs);
    return pContext->in_vqs[port->PortId];
};

__inline
struct
virtqueue*
GetOutQueue (
    IN PVIOSERIAL_PORT port
)
{
    PPORTS_DEVICE    pContext = NULL;
    ASSERT (port);
    ASSERT (port->BusDevice);
    pContext = GetPortsDevice(port->BusDevice);
    ASSERT (pContext->out_vqs);
    return pContext->out_vqs[port->PortId];
};

#endif /* VIOSERIAL_H */
