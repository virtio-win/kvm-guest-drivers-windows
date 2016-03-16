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

#if defined(EVENT_TRACING)
#include "Hid.tmh"
#endif

//
// This is a hard-coded report descriptor for a simple 3-button mouse. Returned
// by the mini driver in response to IOCTL_HID_GET_REPORT_DESCRIPTOR.
//
HID_REPORT_DESCRIPTOR G_DefaultReportDescriptor[] =
{
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x02, // USAGE (Mouse)
    0xa1, 0x01, // COLLECTION (Application)
    0x09, 0x01, //   USAGE (Pointer)
    0xa1, 0x00, //   COLLECTION (Physical)
    0x05, 0x09, //     USAGE_PAGE (Button)
    0x19, 0x01, //     USAGE_MINIMUM (Button 1)
    0x29, 0x03, //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x25, 0x01, //     LOGICAL_MAXIMUM (1)
    0x95, 0x03, //     REPORT_COUNT (3)
    0x75, 0x01, //     REPORT_SIZE (1)
    0x81, 0x02, //     INPUT (Data,Var,Abs)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x75, 0x05, //     REPORT_SIZE (5)
    0x81, 0x03, //     INPUT (Cnst,Var,Abs)
    0x05, 0x01, //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30, //     USAGE (X)
    0x09, 0x31, //     USAGE (Y)
    0x15, 0x81, //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f, //     LOGICAL_MAXIMUM (127)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x02, //     REPORT_COUNT (2)
    0x81, 0x06, //     INPUT (Data,Var,Rel)
    0xc0,       //   END_COLLECTION
    0xc0        // END_COLLECTION
};

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
    {                                       // DescriptorList[0]
        0x22,                               // report descriptor type 0x22
        sizeof(G_DefaultReportDescriptor)   // total length of report descriptor
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTLS,
                "--> %s, code = %d\n", __FUNCTION__, IoControlCode);

    switch (IoControlCode)
    {
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_GET_DEVICE_DESCRIPTOR\n");
        //
        // Return the device's HID descriptor.
        //
        ASSERT(G_DefaultHidDescriptor.bLength != 0);
        status = RequestCopyFromBuffer(Request,
            &G_DefaultHidDescriptor,
            G_DefaultHidDescriptor.bLength);
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
            &G_DefaultReportDescriptor,
            G_DefaultHidDescriptor.DescriptorList[0].wReportLength);
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
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    if (completeRequest)
    {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
}

VOID
ProcessInputEvent(
    PINPUT_DEVICE pContext,
    PVIRTIO_INPUT_EVENT pEvent)
{
    NTSTATUS status;
    WDFREQUEST request;
    unsigned char bit;

    TraceEvents(
        TRACE_LEVEL_VERBOSE,
        DBG_READ,
        "--> %s TYPE: %d, CODE: %d, VALUE: %d\n",
        __FUNCTION__,
        pEvent->type,
        pEvent->code,
        pEvent->value);

    switch (pEvent->type)
    {
    case EV_REL:
        switch (pEvent->code)
        {
        case REL_X: pContext->HidReport.x_axis = (signed char)pEvent->value; break;
        case REL_Y: pContext->HidReport.y_axis = (signed char)pEvent->value; break;
        }
        break;
    case EV_KEY:
        bit = 0;
        switch (pEvent->code)
        {
        case BTN_LEFT: bit = 0x01; break;
        case BTN_RIGHT: bit = 0x02; break;
        case BTN_MIDDLE: bit = 0x04; break;
        }
        if (pEvent->value)
            pContext->HidReport.buttons |= bit;
        else
            pContext->HidReport.buttons &= ~bit;
        break;
    case EV_SYN:
        // send the report up
        status = WdfIoQueueRetrieveNextRequest(pContext->HidQueue, &request);
        if (NT_SUCCESS(status))
        {
            status = RequestCopyFromBuffer(
                request,
                &pContext->HidReport,
                sizeof(pContext->HidReport));

            WdfRequestComplete(request, status);
        }

        // reset all rel fields
        pContext->HidReport.x_axis = 0;
        pContext->HidReport.y_axis = 0;
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "--> %s\n", __FUNCTION__);

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
