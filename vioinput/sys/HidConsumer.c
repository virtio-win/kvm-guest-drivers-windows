/*
 * Consumer control HID functionality
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
#include "HidConsumer.tmh"
#endif

typedef struct _tagInputClassConsumer
{
    INPUT_CLASS_COMMON Common;

    // the consumer HID report is laid out as follows:
    // offset 0
    // * report ID
    // offset 1
    // * consumer controls, one bit per control followed by padding
    //   to the nearest whole byte

    // number of controls supported by the HID report
    ULONG  uNumOfControls;
    // array of control bitmap indices indexed by EVDEV codes
    PULONG pControlMap;
    // length of the pControlMap array in bytes
    SIZE_T cbControlMapLen;
} INPUT_CLASS_CONSUMER, *PINPUT_CLASS_CONSUMER;

NTSTATUS
HIDConsumerEventToReport(
    PINPUT_CLASS_COMMON pClass,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pClass->pHidReport;
    PINPUT_CLASS_CONSUMER pConsumerDesc = (PINPUT_CLASS_CONSUMER)pClass;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pClass->uReportID;
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
                pClass->bDirty = TRUE;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static VOID
HIDConsumerCleanup(
    PINPUT_CLASS_COMMON pClass)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PINPUT_CLASS_CONSUMER pConsumerDesc = (PINPUT_CLASS_CONSUMER)pClass;
    VIOInputFree(&pConsumerDesc->pControlMap);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
HIDConsumerProbe(
    PINPUT_DEVICE pContext,
    PDYNAMIC_ARRAY pHidDesc,
    PVIRTIO_INPUT_CFG_DATA pKeys)
{
    PINPUT_CLASS_CONSUMER pConsumerDesc = NULL;
    NTSTATUS status = STATUS_SUCCESS;
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
        goto Exit;
    }

    // allocate and initialize pConsumerDesc
    pConsumerDesc = VIOInputAlloc(sizeof(INPUT_CLASS_CONSUMER));
    if (pConsumerDesc == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    pConsumerDesc->Common.EventToReportFunc = HIDConsumerEventToReport;
    pConsumerDesc->Common.CleanupFunc = HIDConsumerCleanup;
    pConsumerDesc->Common.uReportID = (UCHAR)(pContext->uNumOfClasses + 1);

    // allocate and initialize the pControlMap
    pConsumerDesc->cbControlMapLen = (SIZE_T)uMaxKeyCode + 1;
    pConsumerDesc->pControlMap = VIOInputAlloc(pConsumerDesc->cbControlMapLen * sizeof(ULONG));
    if (pConsumerDesc->pControlMap == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
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

    // calculate the consumer HID report size
    pConsumerDesc->Common.cbHidReportSize =
        (SIZE_T)HID_REPORT_DATA_OFFSET +
        (pConsumerDesc->uNumOfControls + 7) / 8;

    // register the consumer class
    status = RegisterClass(pContext, &pConsumerDesc->Common);

Exit:
    if (!NT_SUCCESS(status) && pConsumerDesc != NULL)
    {
        pConsumerDesc->Common.CleanupFunc(&pConsumerDesc->Common);
        VIOInputFree(&pConsumerDesc);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s (%08x)\n", __FUNCTION__, status);
    return status;
}
