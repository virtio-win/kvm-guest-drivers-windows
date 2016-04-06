/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: HidKeyboard.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Keyboard specific HID functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "HidKeyboard.tmh"
#endif

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

NTSTATUS
HIDKeyboardBuildReportDescriptor(
    PDYNAMIC_ARRAY pHidDesc,
    PINPUT_CLASS_KEYBOARD pKeyboardDesc,
    PVIRTIO_INPUT_CFG_DATA pKeys,
    PVIRTIO_INPUT_CFG_DATA pLeds)
{
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
        return STATUS_SUCCESS;
    }

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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Created HID keyboard report descriptor with %d keys and %d LEDs\n",
                uMaxKey + 1,
                bGotLed ? (uMaxLed + 1) : 0);

    // set the total keyboard HID report size
    pKeyboardDesc->Common.cbHidReportSize =
        HID_REPORT_DATA_OFFSET +
        2 + // modifiers and padding
        HID_KEYBOARD_KEY_SLOTS;

    // allocate and initialize the keyboard HID report
    pKeyboardDesc->Common.pHidReport = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pKeyboardDesc->Common.cbHidReportSize,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pKeyboardDesc->Common.pHidReport == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pKeyboardDesc->Common.pHidReport, pKeyboardDesc->Common.cbHidReportSize);

    // allocate and initialize the bitmap of currently pressed keys
    pKeyboardDesc->cbKeysPressedLen = ((uMaxKey + 1) + 7) / 8;
    pKeyboardDesc->pKeysPressed = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pKeyboardDesc->cbKeysPressedLen,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pKeyboardDesc->pKeysPressed == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pKeyboardDesc->pKeysPressed, pKeyboardDesc->cbKeysPressedLen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
HIDKeyboardReleaseClass(
    PINPUT_CLASS_KEYBOARD pKeyboardDesc)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (pKeyboardDesc->Common.pHidReport != NULL)
    {
        ExFreePoolWithTag(pKeyboardDesc->Common.pHidReport, VIOINPUT_DRIVER_MEMORY_TAG);
        pKeyboardDesc->Common.pHidReport = NULL;
    }
    if (pKeyboardDesc->pLastOutputReport != NULL)
    {
        ExFreePoolWithTag(pKeyboardDesc->pLastOutputReport, VIOINPUT_DRIVER_MEMORY_TAG);
        pKeyboardDesc->pLastOutputReport = NULL;
    }
    if (pKeyboardDesc->pKeysPressed != NULL)
    {
        ExFreePoolWithTag(pKeyboardDesc->pKeysPressed, VIOINPUT_DRIVER_MEMORY_TAG);
        pKeyboardDesc->pKeysPressed = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
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
            RtlFillMemory(pKeyArray, HID_USAGE_KEYBOARD_ROLLOVER, HID_KEYBOARD_KEY_SLOTS);
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

VOID
HIDKeyboardEventToReport(
    PINPUT_CLASS_KEYBOARD pKeyboardDesc,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pKeyboardDesc->Common.pHidReport;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pKeyboardDesc->Common.uReportID;
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
            pKeyboardDesc->Common.bDirty = TRUE;
        }
        else if (uCode != 0)
        {
            HIDKeyboardEventKeyToReportKey(pKeyboardDesc, (UCHAR)uCode, pEvent->value);
            pKeyboardDesc->Common.bDirty = TRUE;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    pEvent = (PVIRTIO_INPUT_EVENT_WITH_REQUEST)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(VIRTIO_INPUT_EVENT_WITH_REQUEST),
        VIOINPUT_DRIVER_MEMORY_TAG);
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
        status = VIOInputAddOutBuf(pContext->StatusQ, &pEvent->Event);
        WdfSpinLockRelease(pContext->StatusQLock);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
HIDKeyboardReportToEvent(
    PINPUT_DEVICE pContext,
    PINPUT_CLASS_KEYBOARD pKeyboardDesc,
    WDFREQUEST Request,
    PUCHAR pReport,
    ULONG cbReport)
{
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
    if (pReport[HID_REPORT_ID_OFFSET] != pKeyboardDesc->Common.uReportID)
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
                if (uShortPendingLedValue != 0xFF)
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
    if (uShortPendingLedValue != 0xFF)
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
    if (uShortPendingLedValue == 0xFF)
    {
        // nothing was sent up, complete the request now
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}
