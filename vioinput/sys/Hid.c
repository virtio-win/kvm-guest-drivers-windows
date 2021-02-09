/*
 * HID related functionality
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
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

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "Hid.tmh"
#endif


//
// HID descriptor based on this structure is returned by the mini driver in response
// to IOCTL_HID_GET_DEVICE_DESCRIPTOR.
//
static const HID_DESCRIPTOR G_DefaultHidDescriptor =
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
    NTSTATUS        status;
    BOOLEAN         completeRequest = TRUE;
    WDFDEVICE       device = WdfIoQueueGetDevice(Queue);
    PINPUT_DEVICE   pContext = GetDeviceContext(device);
    ULONG           uReportSize;
    HID_XFER_PACKET Packet;
    WDF_REQUEST_PARAMETERS params;
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

    case IOCTL_HID_WRITE_REPORT:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_WRITE_REPORT\n");
        //
        // Write a report to the device, commonly used for controlling keyboard
        // LEDs. We'll complete the request after the host processes all virtio
        // buffers we add to the status queue.
        //

        WDF_REQUEST_PARAMETERS_INIT(&params);
        WdfRequestGetParameters(Request, &params);

        if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            RtlCopyMemory(&Packet, WdfRequestWdmGetIrp(Request)->UserBuffer,
                          sizeof(HID_XFER_PACKET));
            WdfRequestSetInformation(Request, Packet.reportBufferLen);

            status = ProcessOutputReport(pContext, Request, &Packet);
            if (NT_SUCCESS(status))
            {
                completeRequest = FALSE;
            }
        }
        break;

    case IOCTL_HID_GET_FEATURE:
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "IOCTL_HID_GET_FEATURE\n");

        WDF_REQUEST_PARAMETERS_INIT(&params);
        WdfRequestGetParameters(Request, &params);

        if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
        {
            status = STATUS_BUFFER_TOO_SMALL;
        } else
        {
            PHID_XFER_PACKET pFeaturePkt = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

            if (pFeaturePkt == NULL)
            {
                status = STATUS_INVALID_DEVICE_REQUEST;
            } else
            {
                ULONG i;

                status = STATUS_NOT_IMPLEMENTED;
                for (i = 0; i < pContext->uNumOfClasses; i++)
                {
                    if (pContext->InputClasses[i]->GetFeatureFunc)
                    {
                        status = pContext->InputClasses[i]->GetFeatureFunc(pContext->InputClasses[i], pFeaturePkt);
                        if (!NT_SUCCESS(status))
                        {
                            break;
                        }
                    }
                }
            }
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

static VOID
CompleteHIDQueueRequest(
    PINPUT_DEVICE pContext,
    PINPUT_CLASS_COMMON pClass)
{
    WDFREQUEST request;
    NTSTATUS status;

    if (!pClass->bDirty)
    {
        // nothing to do
        return;
    }

    status = WdfIoQueueRetrieveNextRequest(pContext->HidQueue, &request);
    if (NT_SUCCESS(status))
    {
        status = RequestCopyFromBuffer(
            request,
            pClass->pHidReport,
            pClass->cbHidReportSize);
        WdfRequestComplete(request, status);
        if (NT_SUCCESS(status))
        {
            pClass->bDirty = FALSE;
        }
    }
}

VOID
ProcessInputEvent(
    PINPUT_DEVICE pContext,
    PVIRTIO_INPUT_EVENT pEvent)
{
    ULONG i;

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
        // send report(s) up
        for (i = 0; i < pContext->uNumOfClasses; i++)
        {
            // Ask each class if any collection needs before report
            if (pContext->InputClasses[i]->EventToCollectFunc)
            {
                pContext->InputClasses[i]->EventToCollectFunc(
                    pContext->InputClasses[i],
                    pEvent);
            }
            CompleteHIDQueueRequest(pContext, pContext->InputClasses[i]);
        }
    }

    // ask each class to translate the input event into a HID report
    for (i = 0; i < pContext->uNumOfClasses; i++)
    {
        pContext->InputClasses[i]->EventToReportFunc(
            pContext->InputClasses[i],
            pEvent);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
ProcessOutputReport(
    PINPUT_DEVICE pContext,
    WDFREQUEST Request,
    PHID_XFER_PACKET pPacket)
{
    NTSTATUS status = STATUS_NONE_MAPPED;
    ULONG i;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    for (i = 0; i < pContext->uNumOfClasses; i++)
    {
        PINPUT_CLASS_COMMON pClass = pContext->InputClasses[i];
        if (pClass->uReportID == pPacket->reportId)
        {
            if (pClass->ReportToEventFunc)
            {
                status = pClass->ReportToEventFunc(
                    pClass,
                    pContext,
                    Request,
                    pPacket->reportBuffer,
                    pPacket->reportBufferLen);
            }
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID HIDAppend1(PDYNAMIC_ARRAY pArray, UCHAR tag)
{
    DynamicArrayAppend(pArray, &tag, sizeof(tag));
}

VOID HIDAppend2(PDYNAMIC_ARRAY pArray, UCHAR tag, LONG value)
{
    if (value >= -MINCHAR && value <= MAXCHAR)
    {
        HIDAppend1(pArray, tag | 0x01);
        DynamicArrayAppend(pArray, &value, 1);
    }
    else if (value >= -MINSHORT && value <= MAXSHORT)
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

BOOLEAN InputCfgDataHasBit(PVIRTIO_INPUT_CFG_DATA pData, ULONG bit)
{
    if (pData->size <= (bit / 8))
    {
        return FALSE;
    }
    return (pData->u.bitmap[bit / 8] & (1 << (bit % 8)));
}

static BOOLEAN InputCfgDataEmpty(PVIRTIO_INPUT_CFG_DATA pCfgData)
{
    UCHAR i;
    for (i = 0; i < pCfgData->size; i++)
    {
        if (pCfgData->u.bitmap[i] != 0)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static UINT64 ComputeHash(PUCHAR pData, SIZE_T Length)
{
    UINT64 hash = 0;
    SIZE_T i;

    for (i = 0; i < Length / sizeof(UINT64); i++)
    {
        hash ^= *(PUINT64)pData;
        pData += sizeof(UINT64);
    }
    for (i = 0; i < (Length & 7); i++)
    {
        hash ^= (UINT64)*pData << (i * sizeof(UCHAR));
        pData++;
    }
    return hash;
}

NTSTATUS
VIOInputBuildReportDescriptor(PINPUT_DEVICE pContext)
{
    DYNAMIC_ARRAY ReportDescriptor = { NULL };
    NTSTATUS status = STATUS_SUCCESS;
    VIRTIO_INPUT_CFG_DATA KeyData, RelData, AbsData, LedData, MscData;
    SIZE_T cbReportDescriptor;
    UCHAR i, uReportID = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // key/button config
    KeyData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_KEY bits size %d\n", KeyData.size);
    for (i = 0; i < KeyData.size; i++)
    {
        VirtIOWdfDeviceGet(
            &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
            &KeyData.u.bitmap[i], 1);
    }

    // relative axis config
    RelData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_REL);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_REL bits size %d\n", RelData.size);
    for (i = 0; i < RelData.size; i++)
    {
        VirtIOWdfDeviceGet(
            &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
            &RelData.u.bitmap[i], 1);
    }

    // absolute axis config
    AbsData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_ABS bits size %d\n", AbsData.size);
    for (i = 0; i < AbsData.size; i++)
    {
        VirtIOWdfDeviceGet(
            &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
            &AbsData.u.bitmap[i], 1);
    }

    // Misc config
    MscData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_MSC);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_MSC bits size %d\n", MscData.size);
    for (i = 0; i < MscData.size; i++)
    {
        VirtIOWdfDeviceGet(
            &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
            &MscData.u.bitmap[i], 1);
    }

    // if we have any relative axes, we'll expose a mouse device
    // if we have any absolute axes, we may expose a mouse as well
    if (!InputCfgDataEmpty(&RelData) || !InputCfgDataEmpty(&AbsData))
    {
        status = HIDMouseProbe(
            pContext,
            &ReportDescriptor,
            &RelData,
            &AbsData,
            &KeyData);
        if (!NT_SUCCESS(status))
        {
            goto Exit;
        }
    }

    // if we have any absolute axes left, we may expose a joystick
    if (!InputCfgDataEmpty(&AbsData))
    {
        status = HIDJoystickProbe(
            pContext,
            &ReportDescriptor,
            &AbsData,
            &KeyData);
        if (!NT_SUCCESS(status))
        {
            goto Exit;
        }
    }

    // if we have any absolute axes left, we'll expose a tablet device
    if (!InputCfgDataEmpty(&AbsData))
    {
        status = HIDTabletProbe(
            pContext,
            &ReportDescriptor,
            &AbsData,
            &KeyData,
            &MscData);
        if (!NT_SUCCESS(status))
        {
            goto Exit;
        }
    }

    // if we have any keys left, we'll expose a keyboard device
    if (!InputCfgDataEmpty(&KeyData))
    {
        // LED config
        LedData.size = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_EV_BITS, EV_LED);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got EV_LED bits size %d\n", LedData.size);
        for (i = 0; i < LedData.size; i++)
        {
            VirtIOWdfDeviceGet(
                &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
                &LedData.u.bitmap[i], 1);
        }

        status = HIDKeyboardProbe(
            pContext,
            &ReportDescriptor,
            &KeyData,
            &LedData);
        if (!NT_SUCCESS(status))
        {
            goto Exit;
        }
    }

    // if we still have any keys left, we'll check for a consumer device
    if (!InputCfgDataEmpty(&KeyData))
    {
        status = HIDConsumerProbe(
            pContext,
            &ReportDescriptor,
            &KeyData);
        if (!NT_SUCCESS(status))
        {
            goto Exit;
        }
    }

    if (DynamicArrayIsEmpty(&ReportDescriptor))
    {
        // we are not exposing any device
        pContext->HidReportDescriptor = NULL;
    }
    else
    {
        // initialize the HID descriptor
        pContext->HidReportDescriptor = (PHID_REPORT_DESCRIPTOR)DynamicArrayGet(
            &ReportDescriptor,
            &cbReportDescriptor);
        if (!pContext->HidReportDescriptor)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            pContext->HidDescriptor = G_DefaultHidDescriptor;
            pContext->HidDescriptor.DescriptorList[0].wReportLength = (USHORT)cbReportDescriptor;

            pContext->HidReportDescriptorHash = ComputeHash(pContext->HidReportDescriptor, cbReportDescriptor);

            DumpReportDescriptor(pContext->HidReportDescriptor, cbReportDescriptor);
        }
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s (%08x)\n", __FUNCTION__, status);
    return status;
}

VOID
GetAbsAxisInfo(
    PINPUT_DEVICE pContext,
    ULONG uAbsAxis,
    struct virtio_input_absinfo *pAbsInfo)
{
    UCHAR uSize, i;

    RtlZeroMemory(pAbsInfo, sizeof(struct virtio_input_absinfo));
    if (uAbsAxis <= MAXUCHAR)
    {
        uSize = SelectInputConfig(pContext, VIRTIO_INPUT_CFG_ABS_INFO, (UCHAR)uAbsAxis);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got abs axis %d info size %d\n", uAbsAxis, uSize);
        for (i = 0; i < uSize && i < sizeof(struct virtio_input_absinfo); i++)
        {
            VirtIOWdfDeviceGet(
                &pContext->VDevice, offsetof(struct virtio_input_config, u.bitmap[i]),
                (PUCHAR)pAbsInfo + i, 1);
        }
    }
}

NTSTATUS
RegisterClass(
    PINPUT_DEVICE pContext,
    PINPUT_CLASS_COMMON pClass)
{
    if (pContext->uNumOfClasses >= MAX_INPUT_CLASS_COUNT)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // allocate HID report buffer
    if (pClass->pHidReport == NULL)
    {
        pClass->pHidReport = VIOInputAlloc(pClass->cbHidReportSize);
        if (pClass->pHidReport == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // insert the class into our array
    pContext->InputClasses[pContext->uNumOfClasses++] = pClass;
    return STATUS_SUCCESS;
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
