/*
 * Keyboard specific HID functionality
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
#include "HidKeyboard.tmh"
#endif

typedef struct _tagInputClassKeyboard
{
    INPUT_CLASS_COMMON Common;

#define HID_KEYBOARD_KEY_SLOTS 6
    // the keyboard HID report is laid out as follows:
    // offset 0
    // * report ID
    // offset 1
    // * 8 modifiers; one bit per modifier
    // offset 2
    // * padding
    // offset 3
    // * key array of length HID_KEYBOARD_KEY_SLOTS; one byte per key

    // bitmap of currently pressed keys
    PUCHAR pKeysPressed;
    // length of the pKeysPressed bitmap in bytes
    SIZE_T cbKeysPressedLen;
    // number of keys currently pressed
    SIZE_T nKeysPressed;
    // size of the output (host -> device) report
    SIZE_T cbOutputReport;
    // last seen output (host -> device) report
    PUCHAR pLastOutputReport;
} INPUT_CLASS_KEYBOARD, *PINPUT_CLASS_KEYBOARD;

static UCHAR
HIDLEDEventCodeToUsageCode(
    USHORT uEventCode)
{
    switch (uEventCode)
    {
    case LED_NUML:     return HID_USAGE_LED_NUM_LOCK;
    case LED_CAPSL:    return HID_USAGE_LED_CAPS_LOCK;
    case LED_SCROLLL:  return HID_USAGE_LED_SCROLL_LOCK;
    case LED_COMPOSE:  return HID_USAGE_LED_COMPOSE;
    case LED_KANA:     return HID_USAGE_LED_KANA;
    case LED_SLEEP:    return HID_USAGE_LED_STAND_BY;
    case LED_SUSPEND:  return HID_USAGE_LED_SYSTEM_SUSPEND;
    case LED_MUTE:     return HID_USAGE_LED_MUTE;
    case LED_MISC:     return HID_USAGE_LED_GENERIC_INDICATOR;
    case LED_MAIL:     return HID_USAGE_LED_MESSAGE_WAITING;
    case LED_CHARGING: return HID_USAGE_LED_EXTERNAL_POWER_CONNECTED;
    }
    return 0;
}

static USHORT
HIDLEDUsageCodeToEventCode(
    ULONG uCode)
{
    switch (uCode)
    {
    case HID_USAGE_LED_NUM_LOCK:                 return LED_NUML;
    case HID_USAGE_LED_CAPS_LOCK:                return LED_CAPSL;
    case HID_USAGE_LED_SCROLL_LOCK:              return LED_SCROLLL;
    case HID_USAGE_LED_COMPOSE:                  return LED_COMPOSE;
    case HID_USAGE_LED_KANA:                     return LED_KANA;
    case HID_USAGE_LED_STAND_BY:                 return LED_SLEEP;
    case HID_USAGE_LED_SYSTEM_SUSPEND:           return LED_SUSPEND;
    case HID_USAGE_LED_MUTE:                     return LED_MUTE;
    case HID_USAGE_LED_GENERIC_INDICATOR:        return LED_MISC;
    case HID_USAGE_LED_MESSAGE_WAITING:          return LED_MAIL;
    case HID_USAGE_LED_EXTERNAL_POWER_CONNECTED: return LED_CHARGING;
    }
    return 0xFF;
}

static VOID
HIDKeyboardEventKeyToReportKey(
    PINPUT_CLASS_KEYBOARD pKeyboardDesc,
    UCHAR uCode,
    ULONG uValue)
{
    PUCHAR pKeyArray = pKeyboardDesc->Common.pHidReport + HID_REPORT_DATA_OFFSET + 2;
    BOOLEAN bPressed = FALSE, bReleased = FALSE;
    UCHAR uMask, uByte, uBit;
    SIZE_T i, iIndex;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    // figure out the bitmap index and mask
    iIndex = uCode / 8;
    uMask = 1 << (uCode % 8);

    // set or clear the corresponding bitmap bit
    if (uValue)
    {
        if (!(pKeyboardDesc->pKeysPressed[iIndex] & uMask))
        {
            pKeyboardDesc->nKeysPressed++;
            pKeyboardDesc->pKeysPressed[iIndex] |= uMask;
            bPressed = TRUE;
        }
    }
    else
    {
        if (pKeyboardDesc->pKeysPressed[iIndex] & uMask)
        {
            pKeyboardDesc->nKeysPressed--;
            pKeyboardDesc->pKeysPressed[iIndex] &= ~uMask;
            bReleased = TRUE;
        }
    }

    // update the HID report key array
    if (bPressed)
    {
        if (pKeyboardDesc->nKeysPressed <= HID_KEYBOARD_KEY_SLOTS)
        {
            PUCHAR pSlot = memchr(pKeyArray, 0, HID_KEYBOARD_KEY_SLOTS);
            ASSERT(pSlot);
            if (pSlot)
            {
                *pSlot = uCode;
            }
        }
        else if (pKeyboardDesc->nKeysPressed == HID_KEYBOARD_KEY_SLOTS + 1)
        {
            // we just got into the "rolled over" state
#pragma warning (push)
#pragma warning (disable:28625) // C28625 heuristic triggered by "key" in the variable name
            RtlFillMemory(pKeyArray, HID_KEYBOARD_KEY_SLOTS, HID_USAGE_KEYBOARD_ROLLOVER);
#pragma warning (pop)
        }
    }
    else if (bReleased)
    {
        if (pKeyboardDesc->nKeysPressed < HID_KEYBOARD_KEY_SLOTS)
        {
            PUCHAR pSlot = memchr(pKeyArray, uCode, HID_KEYBOARD_KEY_SLOTS);
            ASSERT(pSlot);
            if (pSlot)
            {
                *pSlot = 0;
            }
        }
        else if (pKeyboardDesc->nKeysPressed == HID_KEYBOARD_KEY_SLOTS)
        {
            // we just got out of the "rolled over" state
            i = 0;
            for (iIndex = 0; iIndex < pKeyboardDesc->cbKeysPressedLen; iIndex++)
            {
                uByte = pKeyboardDesc->pKeysPressed[iIndex];
                while (DecodeNextBit(&uByte, &uBit))
                {
                    ASSERT(i < HID_KEYBOARD_KEY_SLOTS);
                    if (i < HID_KEYBOARD_KEY_SLOTS)
                    {
                        pKeyArray[i++] = (UCHAR)(8 * iIndex + uBit);
                    }
                }
            }
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

static NTSTATUS
HIDKeyboardEventToReport(
    PINPUT_CLASS_COMMON pClass,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pClass->pHidReport;
    PINPUT_CLASS_KEYBOARD pKeyboardDesc = (PINPUT_CLASS_KEYBOARD)pClass;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pClass->uReportID;
    if (pEvent->type == EV_KEY)
    {
        ULONG uCode = HIDKeyboardEventCodeToUsageCode(pEvent->code);
        if (uCode >= 0xE0 && uCode <= 0xE7)
        {
            // one bit per modifier, all in one byte
            unsigned char uMask = 1 << (uCode - 0xE0);
            if (pEvent->value)
            {
                pReport[HID_REPORT_DATA_OFFSET] |= uMask;
            }
            else
            {
                pReport[HID_REPORT_DATA_OFFSET] &= ~uMask;
            }
            pClass->bDirty = TRUE;
        }
        else if (uCode != 0)
        {
            HIDKeyboardEventKeyToReportKey(pKeyboardDesc, (UCHAR)uCode, pEvent->value);
            pClass->bDirty = TRUE;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static NTSTATUS
HIDKeyboardSendStatus(
    PINPUT_DEVICE pContext,
    USHORT uType,
    USHORT uCode,
    ULONG uValue,
    WDFREQUEST Request)
{
    PVIRTIO_INPUT_EVENT_WITH_REQUEST pEvent;
    NTSTATUS status;
    PHYSICAL_ADDRESS pa;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    pEvent = pContext->StatusQMemBlock->get_slice(pContext->StatusQMemBlock, &pa);
    if (pEvent == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        pEvent->Event.type = uType;
        pEvent->Event.code = uCode;
        pEvent->Event.value = uValue;
        pEvent->Request = Request;

        WdfSpinLockAcquire(pContext->StatusQLock);
        status = VIOInputAddOutBuf(pContext->StatusQ, &pEvent->Event, pa);
        WdfSpinLockRelease(pContext->StatusQLock);
        if (!NT_SUCCESS(status))
        {
            pContext->StatusQMemBlock->return_slice(pContext->StatusQMemBlock, pEvent);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

static NTSTATUS
HIDKeyboardReportToEvent(
    PINPUT_CLASS_COMMON pClass,
    PINPUT_DEVICE pContext,
    WDFREQUEST Request,
    PUCHAR pReport,
    ULONG cbReport)
{
    PINPUT_CLASS_KEYBOARD pKeyboardDesc = (PINPUT_CLASS_KEYBOARD)pClass;
    USHORT uPendingLedCode = 0xFF;
    USHORT uShortPendingLedValue = 0;
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T i;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (cbReport < pKeyboardDesc->cbOutputReport)
    {
        // unexpected output report size
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (pReport[HID_REPORT_ID_OFFSET] != pClass->uReportID)
    {
        // unexpected output report ID
        return STATUS_INVALID_PARAMETER;
    }

    // diff this report with the last one we've seen
    for (i = HID_REPORT_DATA_OFFSET; i < pKeyboardDesc->cbOutputReport; i++)
    {
        UCHAR uDiff = pKeyboardDesc->pLastOutputReport[i] ^ pReport[i];
        UCHAR uValue;
        while (DecodeNextBit(&uDiff, &uValue))
        {
            ULONG uCode = uValue + 8 * (ULONG)(i - HID_REPORT_DATA_OFFSET);

            // LED codes are 1-based
            USHORT uLedCode = HIDLEDUsageCodeToEventCode(uCode + 1);
            if (uLedCode != 0xFF)
            {
                if (uPendingLedCode != 0xFF)
                {
                    // send the pending LED change to the host
                    status = HIDKeyboardSendStatus(pContext, EV_LED, uPendingLedCode,
                                                   uShortPendingLedValue, NULL);
                    if (!NT_SUCCESS(status))
                    {
                        return status;
                    }
                }
                uPendingLedCode = uLedCode;
                uShortPendingLedValue = !!(pReport[i] & (1 << uValue));
            }
        }
    }

    // send the last pending LED change; this one will complete the request
    if (uPendingLedCode != 0xFF)
    {
        status = HIDKeyboardSendStatus(pContext, EV_LED, uPendingLedCode,
                                       uShortPendingLedValue, Request);
    }

    if (NT_SUCCESS(status))
    {
        // save this report
        RtlCopyMemory(pKeyboardDesc->pLastOutputReport, pReport,
                      pKeyboardDesc->cbOutputReport);
    }
    if (uPendingLedCode == 0xFF)
    {
        // nothing was sent up, complete the request now
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

static VOID
HIDKeyboardCleanup(
    PINPUT_CLASS_COMMON pClass)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PINPUT_CLASS_KEYBOARD pKeyboardDesc = (PINPUT_CLASS_KEYBOARD)pClass;
    VIOInputFree(&pKeyboardDesc->pLastOutputReport);
    VIOInputFree(&pKeyboardDesc->pKeysPressed);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
HIDKeyboardProbe(
    PINPUT_DEVICE pContext,
    PDYNAMIC_ARRAY pHidDesc,
    PVIRTIO_INPUT_CFG_DATA pKeys,
    PVIRTIO_INPUT_CFG_DATA pLeds)
{
    PINPUT_CLASS_KEYBOARD pKeyboardDesc = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR i, uValue, uMaxKey, uMaxLed;
    BOOLEAN bGotKey, bGotLed;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    uMaxKey = 0;
    bGotKey = FALSE;
    for (i = 0; i < pKeys->size; i++)
    {
        UCHAR uNonKeys = 0;
        while (DecodeNextBit(&pKeys->u.bitmap[i], &uValue))
        {
            USHORT uKeyCode = uValue + 8 * i;
            ULONG uCode = HIDKeyboardEventCodeToUsageCode(uKeyCode);
            if (uCode != 0 && (uCode & KEY_TYPE_MASK) == KEY_TYPE_KEYBOARD)
            {
                bGotKey = TRUE;
                if (uCode < 0xE0 || uCode > 0xE7)
                {
                    uMaxKey = max(uMaxKey, (UCHAR)uCode);
                }
            }
            else
            {
                // we have not recognized this EV_KEY code as a keyboard key
                uNonKeys |= (1 << uValue);
            }
        }
        pKeys->u.bitmap[i] = uNonKeys;
    }

    if (!bGotKey)
    {
        // no keys in the array means that we're done
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "No keyboard key found\n");
        goto Exit;
    }

    // allocate and initialize pKeyboardDesc
    pKeyboardDesc = VIOInputAlloc(sizeof(INPUT_CLASS_KEYBOARD));
    if (pKeyboardDesc == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    pKeyboardDesc->Common.EventToReportFunc = HIDKeyboardEventToReport;
    pKeyboardDesc->Common.ReportToEventFunc = HIDKeyboardReportToEvent;
    pKeyboardDesc->Common.CleanupFunc = HIDKeyboardCleanup;
    pKeyboardDesc->Common.uReportID = (UCHAR)(pContext->uNumOfClasses + 1);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_KEYBOARD);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);

    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pKeyboardDesc->Common.uReportID);

    // one bit per modifier
    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_KEYBOARD);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MINIMUM, 0xE0);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MAXIMUM, 0xE7);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x08);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // reserved byte
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x08);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_CONSTANT);

    // LEDs
    uMaxLed = 0;
    bGotLed = FALSE;
    for (i = 0; i < pLeds->size; i++)
    {
        while (DecodeNextBit(&pLeds->u.bitmap[i], &uValue))
        {
            USHORT uLedCode = uValue + 8 * i;
            UCHAR uCode = HIDLEDEventCodeToUsageCode(uLedCode);
            if (uCode != 0)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got LED %d\n", uLedCode);
                uMaxLed = max(uMaxLed, uCode);
                bGotLed = TRUE;
            }
        }
    }
    if (bGotLed)
    {
        ULONG uNumOfLEDs = uMaxLed + 1;
        pKeyboardDesc->cbOutputReport = HID_REPORT_DATA_OFFSET + (uNumOfLEDs + 7) / 8;

        // one bit per LED as usual in a keyboard device
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, uMaxLed + 1);
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_LED);
        HIDAppend2(pHidDesc, HID_TAG_USAGE_MINIMUM, 1);
        HIDAppend2(pHidDesc, HID_TAG_USAGE_MAXIMUM, uMaxLed + 1);
        HIDAppend2(pHidDesc, HID_TAG_OUTPUT, HID_DATA_FLAG_VARIABLE);

        // pad to the nearest whole byte
        if (uNumOfLEDs % 8)
        {
            HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
            HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE,
                       (ULONG)(pKeyboardDesc->cbOutputReport * 8) - uNumOfLEDs);
            HIDAppend2(pHidDesc, HID_TAG_OUTPUT, HID_DATA_FLAG_CONSTANT);
        }

        // allocate and initialize a buffer holding the last seen output report
        pKeyboardDesc->pLastOutputReport = ExAllocatePoolWithTag(
            NonPagedPool,
            pKeyboardDesc->cbOutputReport,
            VIOINPUT_DRIVER_MEMORY_TAG);
        if (pKeyboardDesc->pLastOutputReport == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(pKeyboardDesc->pLastOutputReport, pKeyboardDesc->cbOutputReport);
    }

    // keys
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x08);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, HID_KEYBOARD_KEY_SLOTS);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, uMaxKey);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_KEYBOARD);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MAXIMUM, uMaxKey);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, 0);

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    // allocate and initialize the bitmap of currently pressed keys
    pKeyboardDesc->cbKeysPressedLen = ((uMaxKey + 1) + 7) / 8;
    pKeyboardDesc->pKeysPressed = VIOInputAlloc(pKeyboardDesc->cbKeysPressedLen);
    if (pKeyboardDesc->pKeysPressed == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Created HID keyboard report descriptor with %d keys and %d LEDs\n",
                uMaxKey + 1,
                bGotLed ? (uMaxLed + 1) : 0);

    // calculate the keyboard HID report size
    pKeyboardDesc->Common.cbHidReportSize =
        HID_REPORT_DATA_OFFSET +
        2 + // modifiers and padding
        HID_KEYBOARD_KEY_SLOTS;

    // register the keyboard class
    status = RegisterClass(pContext, &pKeyboardDesc->Common);

Exit:
    if (!NT_SUCCESS(status) && pKeyboardDesc != NULL)
    {
        pKeyboardDesc->Common.CleanupFunc(&pKeyboardDesc->Common);
        VIOInputFree(&pKeyboardDesc);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s (%08x)\n", __FUNCTION__, status);
    return status;
}
