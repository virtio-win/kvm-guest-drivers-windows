/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: Hid.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * HID related functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "Hid.tmh"
#endif


//
// This is a hard-coded HID descriptor for a simple 3-button mouse. Returned
// by the mini driver in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of G_DefaultReportDescriptor.
//
HID_DESCRIPTOR G_DefaultHidDescriptor =
{
    0x09,   // length of HID descriptor
    0x21,   // descriptor type == HID  0x21
    0x0100, // hid spec release
    0x00,   // country code == Not Specified
    0x01,   // number of HID class descriptors
    {              // DescriptorList[0]
        0x22,      // report descriptor type 0x22
        0x0000     // total length of report descriptor
    }
};

VOID
EvtIoDeviceControl(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     OutputBufferLength,
    size_t     InputBufferLength,
    ULONG      IoControlCode)
{
    NTSTATUS      status;
    BOOLEAN       completeRequest = TRUE;
    WDFDEVICE     device = WdfIoQueueGetDevice(Queue);
    PINPUT_DEVICE pContext = GetDeviceContext(device);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS,
                "--> %s, code = %d\n", __FUNCTION__, IoControlCode);

    switch (IoControlCode)
    {
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_GET_DEVICE_DESCRIPTOR\n");
        //
        // Return the device's HID descriptor.
        //
        ASSERT(pContext->HidDescriptor.bLength != 0);
        status = RequestCopyFromBuffer(Request,
            &pContext->HidDescriptor,
            pContext->HidDescriptor.bLength);
        break;

    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_GET_DEVICE_ATTRIBUTES\n");
        //
        // Return the device's attributes in a HID_DEVICE_ATTRIBUTES structure.
        //
        status = RequestCopyFromBuffer(Request,
            &pContext->HidDeviceAttributes,
            sizeof(HID_DEVICE_ATTRIBUTES));
        break;

    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_GET_REPORT_DESCRIPTOR\n");
        //
        // Return the report descriptor for the HID device.
        //
        status = RequestCopyFromBuffer(Request,
            pContext->HidReportDescriptor,
            pContext->HidDescriptor.DescriptorList[0].wReportLength);
        break;

    case IOCTL_HID_READ_REPORT:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_READ_REPORT\n");
        //
        // Queue up a report request. We'll complete it when we actually
        // receive data from the device.
        //
        status = WdfRequestForwardToIoQueue(
            Request,
            pContext->HidQueue);
        if (NT_SUCCESS(status))
        {
            completeRequest = FALSE;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                        "WdfRequestForwardToIoQueue failed with 0x%x\n", status);
        }
        break;

    default:
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTLS,
                    "Unrecognized IOCTL %d\n", IoControlCode);
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    if (completeRequest)
    {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
}

VOID
ProcessInputEvent(
    PINPUT_DEVICE pContext,
    PVIRTIO_INPUT_EVENT pEvent)
{
    NTSTATUS status;
    WDFREQUEST request;

    TraceEvents(
        TRACE_LEVEL_VERBOSE,
        DBG_READ,
        "--> %s TYPE: %d, CODE: %d, VALUE: %d\n",
        __FUNCTION__,
        pEvent->type,
        pEvent->code,
        pEvent->value);

    if (pEvent->type == EV_SYN)
    {
        // send the report up
        status = WdfIoQueueRetrieveNextRequest(pContext->HidQueue, &request);
        if (NT_SUCCESS(status))
        {
            status = RequestCopyFromBuffer(
                request,
                pContext->HidReport,
                pContext->HidReportSize);

            WdfRequestComplete(request, status);
        }
    }

    HIDMouseEventToReport(
        &pContext->MouseDesc,
        pEvent,
        pContext->HidReport + pContext->MouseDesc.Common.cbHidReportOffset);

    // TODO: keyboard, tablet, joystick, ...

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

VOID HIDAppend1(PDYNAMIC_ARRAY pArray, UCHAR tag)
{
    DynamicArrayAppend(pArray, &tag, sizeof(tag));
}

VOID HIDAppend2(PDYNAMIC_ARRAY pArray, UCHAR tag, ULONG value)
{
    if (value < MAXUCHAR)
    {
        HIDAppend1(pArray, tag | 0x01);
        DynamicArrayAppend(pArray, &value, 1);
    }
    else if (value < MAXUSHORT)
    {
        HIDAppend1(pArray, tag | 0x02);
        DynamicArrayAppend(pArray, &value, 2);
    }
    else
    {
        HIDAppend1(pArray, tag | 0x03);
        DynamicArrayAppend(pArray, &value, 4);
    }
}

BOOLEAN DecodeNextBit(PUCHAR pBitmap, PUCHAR pValue)
{
    ULONG Index;
    BOOLEAN bResult = BitScanForward(&Index, *pBitmap);
    if (bResult)
    {
        *pValue = (unsigned char)Index;
        *pBitmap &= ~(1 << Index);
    }
    return bResult;
}

static void DumpReportDescriptor(PHID_REPORT_DESCRIPTOR pDescriptor, SIZE_T cbSize)
{
    SIZE_T i = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "HID report descriptor begin\n");
    while (i < cbSize)
    {
        CHAR buffer[20];
        switch (pDescriptor[i] & 0x03)
        {
        case 0x00:
        {
            sprintf_s(buffer, sizeof(buffer), "%02x", pDescriptor[i]);
            i++;
            break;
        }
        case 0x01:
        {
            sprintf_s(buffer, sizeof(buffer), "%02x %02x",
                      pDescriptor[i], pDescriptor[i + 1]);
            i += 2;
            break;
        }
        case 0x02:
        {
            sprintf_s(buffer, sizeof(buffer), "%02x %02x %02x",
                      pDescriptor[i], pDescriptor[i + 1], pDescriptor[i + 2]);
            i += 3;
            break;
        }
        case 0x03:
        {
            sprintf_s(buffer, sizeof(buffer), "%02x %02x %02x %02x %02x",
                      pDescriptor[i], pDescriptor[i + 1], pDescriptor[i + 2],
                      pDescriptor[i + 3], pDescriptor[i + 4]);
            i += 5;
            break;
        }
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s\n", buffer);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "HID report descriptor end\n");
}

static u8 SelectInputConfig(PINPUT_DEVICE pContext, u8 cfgSelect, u8 cfgSubSel)
{
    u8 size = 0;

    VirtIOWdfDeviceSet(
        &pContext->VDevice, offsetof(struct virtio_input_config, select),
        &cfgSelect, sizeof(cfgSelect));
    VirtIOWdfDeviceSet(
        &pContext->VDevice, offsetof(struct virtio_input_config, subsel),
        &cfgSubSel, sizeof(cfgSubSel));

    VirtIOWdfDeviceGet(
        &pContext->VDevice, offsetof(struct virtio_input_config, size),
        &size, sizeof(size));
    return size;
}

NTSTATUS
VIOInputBuildReportDescriptor(PINPUT_DEVICE pContext)
{
    DYNAMIC_ARRAY ReportDescriptor = { NULL };
    NTSTATUS status = STATUS_SUCCESS;
    VIRTIO_INPUT_CFG_DATA KeyData, RelData;
    SIZE_T cbReportDescriptor;
    UCHAR i;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // key/button config
    KeyData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_KEY bits size %d\n", KeyData.size);
    if (KeyData.size > 0)
    {
        for (i = 0; i < KeyData.size; i++)
        {
            VirtIOWdfDeviceGet(
                &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
                &KeyData.u.bitmap[i], 1);
        }
    }

    // relative axis config
    RelData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_REL);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_REL bits size %d\n", RelData.size);
    if (RelData.size > 0)
    {
        for (i = 0; i < RelData.size; i++)
        {
            VirtIOWdfDeviceGet(
                &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
                &RelData.u.bitmap[i], 1);
        }
    }

    // if we have any relative axes, we'll expose a mouse device
    if (RelData.size > 0)
    {
        pContext->MouseDesc.Common.cbHidReportOffset = pContext->HidReportSize;
        status = HIDMouseBuildReportDescriptor(
            &ReportDescriptor,
            &pContext->MouseDesc,
            &RelData,
            &KeyData);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
        pContext->HidReportSize += pContext->MouseDesc.Common.cbHidReportSize;
    }

    // TODO: keyboard, tablet, joystick, ...

    pContext->HidReport = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pContext->HidReportSize,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (!pContext->HidReport)
    {
        DynamicArrayDestroy(&ReportDescriptor);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pContext->HidReport, pContext->HidReportSize);

    // initialize the HID descriptor
    pContext->HidReportDescriptor = (PHID_REPORT_DESCRIPTOR)DynamicArrayGet(
        &ReportDescriptor,
        &cbReportDescriptor);
    if (!pContext->HidReportDescriptor)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pContext->HidDescriptor = G_DefaultHidDescriptor;
    pContext->HidDescriptor.DescriptorList[0].wReportLength = (USHORT)cbReportDescriptor;

    DumpReportDescriptor(pContext->HidReportDescriptor, cbReportDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
RequestCopyFromBuffer(
    WDFREQUEST Request,
    PVOID      SourceBuffer,
    size_t     NumBytesToCopyFrom)
{
    NTSTATUS  status;
    WDFMEMORY memory;
    size_t    outputBufferLength;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "<-- %s\n", __FUNCTION__);
        return status;
    }

    WdfMemoryGetBuffer(memory, &outputBufferLength);
    if (outputBufferLength < NumBytesToCopyFrom)
    {
        status = STATUS_INVALID_BUFFER_SIZE;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                    "RequestCopyFromBuffer: buffer too small. Size %d, expect %d\n",
                    (int)outputBufferLength, (int)NumBytesToCopyFrom);
        return status;
    }

    status = WdfMemoryCopyFromBuffer(memory,
        0,
        SourceBuffer,
        NumBytesToCopyFrom);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                    "WdfMemoryCopyFromBuffer failed 0x%x\n", status);
        return status;
    }

    WdfRequestSetInformation(Request, NumBytesToCopyFrom);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return status;
}
