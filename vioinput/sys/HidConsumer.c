/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: HidConsumer.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Consumer control HID functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "HidConsumer.tmh"
#endif

NTSTATUS
HIDConsumerBuildReportDescriptor(
    PDYNAMIC_ARRAY pHidDesc,
    PINPUT_CLASS_CONSUMER pConsumerDesc,
    PVIRTIO_INPUT_CFG_DATA pKeys)
{
    UCHAR i, uValue;
    USHORT uMaxKeyCode;
    BOOLEAN bGotKey;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // first pass over the pKeys data - find the max key code we need to handle
    uMaxKeyCode = 0;
    bGotKey = FALSE;
    for (i = 0; i < pKeys->size; i++)
    {
        UCHAR uBitmap = pKeys->u.bitmap[i];
        while (DecodeNextBit(&uBitmap, &uValue))
        {
            USHORT uKeyCode = uValue + 8 * i;
            ULONG uCode = HIDKeyboardEventCodeToUsageCode(uKeyCode);
            if (uCode != 0 && (uCode & KEY_TYPE_MASK) == KEY_TYPE_CONSUMER)
            {
                uMaxKeyCode = max(uMaxKeyCode, uKeyCode);
                bGotKey = TRUE;
            }
        }
    }
    if (!bGotKey)
    {
        // no keys in the array means that we're done
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "No consumer key found\n");
        return STATUS_SUCCESS;
    }

    // allocate and initialize the pControlMap
    pConsumerDesc->cbControlMapLen = uMaxKeyCode + 1;
    pConsumerDesc->pControlMap = (PULONG)ExAllocatePoolWithTag(
        NonPagedPool,
        pConsumerDesc->cbControlMapLen * sizeof(ULONG),
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pConsumerDesc->pControlMap == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlFillMemory(pConsumerDesc->pControlMap, pConsumerDesc->cbControlMapLen * sizeof(ULONG), 0xFF);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_CONSUMER);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_CONSUMERCTRL);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);

    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pConsumerDesc->Common.uReportID);

    // one bit per control
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);

    // second pass over the pKeys data - emit HID usages and populate the pControlMap
    pConsumerDesc->uNumOfControls = 0;
    for (i = 0; i < pKeys->size; i++)
    {
        UCHAR uNonControls = 0;
        while (DecodeNextBit(&pKeys->u.bitmap[i], &uValue))
        {
            USHORT uKeyCode = uValue + 8 * i;
            ULONG uCode = HIDKeyboardEventCodeToUsageCode(uKeyCode);
            if (uCode != 0 && (uCode & KEY_TYPE_MASK) == KEY_TYPE_CONSUMER)
            {
                HIDAppend2(pHidDesc, HID_TAG_USAGE, uCode & KEY_USAGE_MASK);
                pConsumerDesc->pControlMap[uKeyCode] = pConsumerDesc->uNumOfControls++;
            }
            else
            {
                // we have not recognized this EV_KEY code as a consumer control
                uNonControls |= (1 << uValue);
            }
        }
        pKeys->u.bitmap[i] = uNonControls;
    }

    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, pConsumerDesc->uNumOfControls);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // pad to the nearest whole byte
    if (pConsumerDesc->uNumOfControls % 8)
    {
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 8 - (pConsumerDesc->uNumOfControls % 8));
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);
    }

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Created HID consumer report descriptor with %d controls\n",
                pConsumerDesc->uNumOfControls);

    // set the total consumer HID report size
    pConsumerDesc->Common.cbHidReportSize =
        HID_REPORT_DATA_OFFSET +
        (pConsumerDesc->uNumOfControls + 7) / 8;

    // allocate and initialize the consumer HID report
    pConsumerDesc->Common.pHidReport = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pConsumerDesc->Common.cbHidReportSize,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pConsumerDesc->Common.pHidReport == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pConsumerDesc->Common.pHidReport, pConsumerDesc->Common.cbHidReportSize);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
HIDConsumerReleaseClass(
    PINPUT_CLASS_CONSUMER pConsumerDesc)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (pConsumerDesc->Common.pHidReport != NULL)
    {
        ExFreePoolWithTag(pConsumerDesc->Common.pHidReport, VIOINPUT_DRIVER_MEMORY_TAG);
        pConsumerDesc->Common.pHidReport = NULL;
    }
    if (pConsumerDesc->pControlMap != NULL)
    {
        ExFreePoolWithTag(pConsumerDesc->pControlMap, VIOINPUT_DRIVER_MEMORY_TAG);
        pConsumerDesc->pControlMap = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

VOID
HIDConsumerEventToReport(
    PINPUT_CLASS_CONSUMER pConsumerDesc,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pConsumerDesc->Common.pHidReport;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pConsumerDesc->Common.uReportID;
    if (pEvent->type == EV_KEY && pEvent->code < pConsumerDesc->cbControlMapLen)
    {
        ULONG uCode = HIDKeyboardEventCodeToUsageCode(pEvent->code);
        if (uCode != 0 && (uCode & KEY_TYPE_MASK) == KEY_TYPE_CONSUMER)
        {
            // use the EVDEV code as an index into pControlMap
            ULONG uBitIndex = pConsumerDesc->pControlMap[pEvent->code];
            if (uBitIndex < pConsumerDesc->uNumOfControls)
            {
                // and simply set or clear the corresponding bit
                ULONG uOffset = uBitIndex / 8;
                UCHAR uBit = 1 << (uBitIndex % 8);
                if (pEvent->value)
                {
                    pReport[HID_REPORT_DATA_OFFSET + uOffset] |= uBit;
                }
                else
                {
                    pReport[HID_REPORT_DATA_OFFSET + uOffset] &= ~uBit;
                }
                pConsumerDesc->Common.bDirty = TRUE;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}
