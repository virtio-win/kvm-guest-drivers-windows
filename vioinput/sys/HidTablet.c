/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: HidTablet.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Tablet specific HID functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "HidTablet.tmh"
#endif

typedef struct _tagInputClassTablet
{
    INPUT_CLASS_COMMON Common;

    // the tablet HID report is laid out as follows:
    // offset 0
    // * report ID
    // offset 1
    // * switches and flags
    // ** bit 0 tip switch
    // ** bit 1 in range
    // ** bit 2 barrel switch
    // ** bits 3-7 padding
    // offset 2
    // * X axis, 2 bytes, absolute
    // offset 4
    // * Y axis, 2 bytes, absolute

} INPUT_CLASS_TABLET, *PINPUT_CLASS_TABLET;

static NTSTATUS
HIDTabletEventToReport(
    PINPUT_CLASS_COMMON pClass,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pClass->pHidReport;
    PUSHORT pAxisReport;
    UCHAR uBits;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pClass->uReportID;
    switch (pEvent->type)
    {
    case EV_ABS:
        switch (pEvent->code)
        {
        case ABS_X:
            pAxisReport = (USHORT *)&pReport[HID_REPORT_DATA_OFFSET + 1];
            break;
        case ABS_Y:
            pAxisReport = (USHORT *)&pReport[HID_REPORT_DATA_OFFSET + 3];
            break;
        default:
            pAxisReport = NULL;
            break;
        }
        if (pAxisReport != NULL)
        {
            *pAxisReport = (USHORT)pEvent->value;
            pClass->bDirty = TRUE;
        }
        break;
    case EV_KEY:
        switch (pEvent->code)
        {
        case BTN_LEFT:
        case BTN_TOUCH:
            // tip switch + in range
            uBits = 0x03;
            break;
        case BTN_RIGHT:
        case BTN_STYLUS:
            // barrel switch
            uBits = 0x04;
            break;
        default:
            uBits = 0x00;
            break;
        }
        if (uBits)
        {
            if (pEvent->value)
            {
                pReport[HID_REPORT_DATA_OFFSET] |= uBits;
            }
            else
            {
                pReport[HID_REPORT_DATA_OFFSET] &= ~uBits;
            }
            pClass->bDirty = TRUE;
        }
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
HIDTabletProbe(
    PINPUT_DEVICE pContext,
    PDYNAMIC_ARRAY pHidDesc,
    PVIRTIO_INPUT_CFG_DATA pAxes,
    PVIRTIO_INPUT_CFG_DATA pButtons)
{
    PINPUT_CLASS_TABLET pTabletDesc = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR i, uValue;
    ULONG uAxisCode, uNumOfAbsAxes = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // we expect to see two absolute axes, X and Y
    for (i = 0; i < pAxes->size; i++)
    {
        UCHAR uNonAxes = 0;
        while (DecodeNextBit(&pAxes->u.bitmap[i], &uValue))
        {
            USHORT uAxisCode = uValue + 8 * i;
            if (uAxisCode == ABS_X || uAxisCode == ABS_Y)
            {
                uNumOfAbsAxes++;
            }
            else
            {
                uNonAxes |= (1 << uValue);
            }
        }
        pAxes->u.bitmap[i] = uNonAxes;
    }

    if (uNumOfAbsAxes != 2)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Tablet axes not found\n");
        goto Exit;
    }

    // claim our buttons from the pAxes bitmap
    for (i = 0; i < pButtons->size; i++)
    {
        UCHAR uNonButtons = 0;
        while (DecodeNextBit(&pAxes->u.bitmap[i], &uValue))
        {
            USHORT uAxisCode = uValue + 8 * i;
            // a few hard-coded buttons we understand
            switch (uAxisCode)
            {
            case BTN_LEFT:
            case BTN_RIGHT:
            case BTN_TOUCH:
            case BTN_STYLUS:
                break;
            default:
                uNonButtons |= (1 << uValue);
            }
        }
        pAxes->u.bitmap[i] = uNonButtons;
    }

    // allocate and initialize pTabletDesc
    pTabletDesc = VIOInputAlloc(sizeof(INPUT_CLASS_TABLET));
    if (pTabletDesc == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    pTabletDesc->Common.EventToReportFunc = HIDTabletEventToReport;
    pTabletDesc->Common.uReportID = (UCHAR)(pContext->uNumOfClasses + 1);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_DIGITIZER);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_STYLUS);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_PHYSICAL);

    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pTabletDesc->Common.uReportID);

    // tip switch, one bit
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_TIP_SWITCH);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // in range flag, one bit
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_IN_RANGE);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // barrel switch, one bit
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_BARREL_SWITCH);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // padding
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x05);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_POINTER);

    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_PHYSICAL);
    for (uAxisCode = ABS_X; uAxisCode <= ABS_Y; uAxisCode++)
    {
        struct virtio_input_absinfo AbsInfo;
        GetAbsAxisInfo(pContext, uAxisCode, &AbsInfo);

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got abs axis %d, min %d, max %d\n",
                    uAxisCode, AbsInfo.min, AbsInfo.max);

        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, AbsInfo.min);
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, AbsInfo.max);

        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x10);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_UNIT_EXPONENT, 0x0D); // -3
        HIDAppend2(pHidDesc, HID_TAG_UNIT, 0x00);          // none

        HIDAppend2(pHidDesc, HID_TAG_USAGE,
                   (uAxisCode == ABS_X ? HID_USAGE_GENERIC_X : HID_USAGE_GENERIC_Y));

        HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MINIMUM, AbsInfo.min);
        HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MAXIMUM, AbsInfo.max);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);
    }
    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);
    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Created HID tablet report descriptor\n");

    // calculate the tablet HID report size
    pTabletDesc->Common.cbHidReportSize =
        1 + // report ID
        1 + // flags
        uNumOfAbsAxes * 2; // axes

    // register the tablet class
    status = RegisterClass(pContext, &pTabletDesc->Common);

Exit:
    if (!NT_SUCCESS(status) && pTabletDesc != NULL)
    {
        VIOInputFree(&pTabletDesc);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s (%08x)\n", __FUNCTION__, status);
    return status;
}
