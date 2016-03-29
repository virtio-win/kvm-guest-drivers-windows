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

static const UCHAR ReportCodeToUsageCodeTable[] =
{
    /* KEY_RESERVED */           0x00,
    /* KEY_ESC */                0x29,
    /* KEY_1 */                  0x1E,
    /* KEY_2 */                  0x1F,
    /* KEY_3 */                  0x20,
    /* KEY_4 */                  0x21,
    /* KEY_5 */                  0x22,
    /* KEY_6 */                  0x23,
    /* KEY_7 */                  0x24,
    /* KEY_8 */                  0x25,
    /* KEY_9 */                  0x26,
    /* KEY_0 */                  0x27,
    /* KEY_MINUS */              0x2D,
    /* KEY_EQUAL */              0x2E,
    /* KEY_BACKSPACE */          0x2A,
    /* KEY_TAB */                0x2B,
    /* KEY_Q */                  0x14,
    /* KEY_W */                  0x1A,
    /* KEY_E */                  0x08,
    /* KEY_R */                  0x15,
    /* KEY_T */                  0x17,
    /* KEY_Y */                  0x1C,
    /* KEY_U */                  0x18,
    /* KEY_I */                  0x0C,
    /* KEY_O */                  0x12,
    /* KEY_P */                  0x13,
    /* KEY_LEFTBRACE */          0x2F,
    /* KEY_RIGHTBRACE */         0x30,
    /* KEY_ENTER */              0x28,
    /* KEY_LEFTCTRL */           0xE0,
    /* KEY_A */                  0x04,
    /* KEY_S */                  0x16,
    /* KEY_D */                  0x07,
    /* KEY_F */                  0x09,
    /* KEY_G */                  0x0A,
    /* KEY_H */                  0x0B,
    /* KEY_J */                  0x0D,
    /* KEY_K */                  0x0E,
    /* KEY_L */                  0x0F,
    /* KEY_SEMICOLON */          0x33,
    /* KEY_APOSTROPHE */         0x34,
    /* KEY_GRAVE */              0x35,
    /* KEY_LEFTSHIFT */          0xE1,
    /* KEY_BACKSLASH */          0x32,
    /* KEY_Z */                  0x1D,
    /* KEY_X */                  0x1B,
    /* KEY_C */                  0x06,
    /* KEY_V */                  0x19,
    /* KEY_B */                  0x05,
    /* KEY_N */                  0x11,
    /* KEY_M */                  0x10,
    /* KEY_COMMA */              0x36,
    /* KEY_DOT */                0x37,
    /* KEY_SLASH */              0x38,
    /* KEY_RIGHTSHIFT */         0xE5,
    /* KEY_KPASTERISK */         0x55,
    /* KEY_LEFTALT */            0xE2,
    /* KEY_SPACE */              0x2C,
    /* KEY_CAPSLOCK */           0x39,
    /* KEY_F1 */                 0x3A,
    /* KEY_F2 */                 0x3B,
    /* KEY_F3 */                 0x3C,
    /* KEY_F4 */                 0x3D,
    /* KEY_F5 */                 0x3E,
    /* KEY_F6 */                 0x3F,
    /* KEY_F7 */                 0x40,
    /* KEY_F8 */                 0x41,
    /* KEY_F9 */                 0x42,
    /* KEY_F10 */                0x43,
    /* KEY_NUMLOCK */            0x53,
    /* KEY_SCROLLLOCK */         0x47,
    /* KEY_KP7 */                0x5F,
    /* KEY_KP8 */                0x60,
    /* KEY_KP9 */                0x61,
    /* KEY_KPMINUS */            0x56,
    /* KEY_KP4 */                0x5C,
    /* KEY_KP5 */                0x5D,
    /* KEY_KP6 */                0x5E,
    /* KEY_KPPLUS */             0x57,
    /* KEY_KP1 */                0x59,
    /* KEY_KP2 */                0x5A,
    /* KEY_KP3 */                0x5B,
    /* KEY_KP0 */                0x62,
    /* KEY_KPDOT */              0x63,
    /* unused */                 0x00,
    /* KEY_ZENKAKUHANKAKU */     0x94,
    /* KEY_102ND */              0x64,
    /* KEY_F11 */                0x44,
    /* KEY_F12 */                0x45,
    /* KEY_RO */                 0x87,
    /* KEY_KATAKANA */           0x92,
    /* KEY_HIRAGANA */           0x93,
    /* KEY_HENKAN */             0x8A,
    /* KEY_KATAKANAHIRAGANA */   0x88,
    /* KEY_MUHENKAN */           0x8B,
    /* KEY_KPJPCOMMA */          0x8C,
    /* KEY_KPENTER */            0x58,
    /* KEY_RIGHTCTRL */          0xE4,
    /* KEY_KPSLASH */            0x54,
    /* KEY_SYSRQ */              0x46,
    /* KEY_RIGHTALT */           0xE6,
    /* KEY_LINEFEED */           0x00,
    /* KEY_HOME */               0x4A,
    /* KEY_UP */                 0x52,
    /* KEY_PAGEUP */             0x4B,
    /* KEY_LEFT */               0x50,
    /* KEY_RIGHT */              0x4F,
    /* KEY_END */                0x4D,
    /* KEY_DOWN */               0x51,
    /* KEY_PAGEDOWN */           0x4E,
    /* KEY_INSERT */             0x49,
    /* KEY_DELETE */             0x4C,
    /* KEY_MACRO */              0x00,
    /* KEY_MUTE */               0xEF,
    /* KEY_VOLUMEDOWN */         0xEE,
    /* KEY_VOLUMEUP */           0xED,
    /* KEY_POWER */              0x66,
    /* KEY_KPEQUAL */            0x67,
    /* KEY_KPPLUSMINUS */        0x00,
    /* KEY_PAUSE */              0x48,
    /* KEY_SCALE */              0x00,
    /* KEY_KPCOMMA */            0x85,
    /* KEY_HANGEUL */            0x90,
    /* KEY_HANJA */              0x91,
    /* KEY_YEN */                0x89,
    /* KEY_LEFTMETA */           0xE3,
    /* KEY_RIGHTMETA */          0xE7,
    /* KEY_COMPOSE */            0x65,
    /* KEY_STOP */               0xF3,
    /* KEY_AGAIN */              0x79,
    /* KEY_PROPS */              0x76,
    /* KEY_UNDO */               0x7A,
    /* KEY_FRONT */              0x77,
    /* KEY_COPY */               0x7C,
    /* KEY_OPEN */               0x74,
    /* KEY_PASTE */              0x7D,
    /* KEY_FIND */               0xF4,
    /* KEY_CUT */                0x7B,
    /* KEY_HELP */               0x75,
    /* KEY_MENU */               0x65,
    /* KEY_CALC */               0xFB,
    /* KEY_SETUP */              0x00,
    /* KEY_SLEEP */              0xF8,
    /* KEY_WAKEUP */             0x00,
    /* KEY_FILE */               0x00,
    /* KEY_SENDFILE */           0x00,
    /* KEY_DELETEFILE */         0x00,
    /* KEY_XFER */               0x00,
    /* KEY_PROG1 */              0x00,
    /* KEY_PROG2 */              0x00,
    /* KEY_WWW */                0xF0,
    /* KEY_MSDOS */              0x00,
    /* KEY_COFFEE */             0xF9,
    /* KEY_ROTATE_DISPLAY */     0x00,
    /* KEY_CYCLEWINDOWS */       0x00,
    /* KEY_MAIL */               0x00,
    /* KEY_BOOKMARKS */          0x00,
    /* KEY_COMPUTER */           0x00,
    /* KEY_BACK */               0xF1,
    /* KEY_FORWARD */            0xF2,
    /* KEY_CLOSECD */            0x00,
    /* KEY_EJECTCD */            0xEC,
    /* KEY_EJECTCLOSECD */       0x00,
    /* KEY_NEXTSONG */           0xEB,
    /* KEY_PLAYPAUSE */          0xE8,
    /* KEY_PREVIOUSSONG */       0xEA,
    /* KEY_STOPCD */             0xE9,
    /* KEY_RECORD */             0x00,
    /* KEY_REWIND */             0x00,
    /* KEY_PHONE */              0x00,
    /* KEY_ISO */                0x00,
    /* KEY_CONFIG */             0x00,
    /* KEY_HOMEPAGE */           0x00,
    /* KEY_REFRESH */            0xFA,
    /* KEY_EXIT */               0x00,
    /* KEY_MOVE */               0x00,
    /* KEY_EDIT */               0xF7,
    /* KEY_SCROLLUP */           0xF5,
    /* KEY_SCROLLDOWN */         0xF6,
    /* KEY_KPLEFTPAREN */        0xB6,
    /* KEY_KPRIGHTPAREN */       0xB7,
    /* KEY_NEW */                0x00,
    /* KEY_REDO */               0x00,
    /* KEY_F13 */                0x68,
    /* KEY_F14 */                0x69,
    /* KEY_F15 */                0x6A,
    /* KEY_F16 */                0x6B,
    /* KEY_F17 */                0x6C,
    /* KEY_F18 */                0x6D,
    /* KEY_F19 */                0x6E,
    /* KEY_F20 */                0x6F,
    /* KEY_F21 */                0x70,
    /* KEY_F22 */                0x71,
    /* KEY_F23 */                0x72,
    /* KEY_F24 */                0x73,
    /* unused */                 0x00,
    /* unused */                 0x00,
    /* unused */                 0x00,
    /* unused */                 0x00,
    /* unused */                 0x00,
    /* KEY_PLAYCD */             0x00,
    /* KEY_PAUSECD */            0x00,
    /* KEY_PROG3 */              0x00,
    /* KEY_PROG4 */              0x00,
    /* KEY_DASHBOARD */          0x00,
    /* KEY_SUSPEND */            0x00,
    /* KEY_CLOSE */              0x00,
    /* KEY_PLAY */               0x00,
    /* KEY_FASTFORWARD */        0x00,
    /* KEY_BASSBOOST */          0x00,
    /* KEY_PRINT */              0x00,
    /* KEY_HP */                 0x00,
    /* KEY_CAMERA */             0x00,
    /* KEY_SOUND */              0x00,
    /* KEY_QUESTION */           0x00,
    /* KEY_EMAIL */              0x00,
    /* KEY_CHAT */               0x00,
    /* KEY_SEARCH */             0x00,
    /* KEY_CONNECT */            0x00,
    /* KEY_FINANCE */            0x00,
    /* KEY_SPORT */              0x00,
    /* KEY_SHOP */               0x00,
    /* KEY_ALTERASE */           0x00,
    /* KEY_CANCEL */             0x00,
    /* KEY_BRIGHTNESSDOWN */     0x00,
    /* KEY_BRIGHTNESSUP */       0x00,
    /* KEY_MEDIA */              0x00,
    /* KEY_SWITCHVIDEOMODE */    0x00,
    /* KEY_KBDILLUMTOGGLE */     0x00,
    /* KEY_KBDILLUMDOWN */       0x00,
    /* KEY_KBDILLUMUP */         0x00,
    /* KEY_SEND */               0x00,
    /* KEY_REPLY */              0x00,
    /* KEY_FORWARDMAIL */        0x00,
    /* KEY_SAVE */               0x00,
    /* KEY_DOCUMENTS */          0x00,
    /* KEY_BATTERY */            0x00,
    /* KEY_BLUETOOTH */          0x00,
    /* KEY_WLAN */               0x00,
    /* KEY_UWB */                0x00,
    /* KEY_UNKNOWN */            0x00,
    /* KEY_VIDEO_NEXT */         0x00,
    /* KEY_VIDEO_PREV */         0x00,
    /* KEY_BRIGHTNESS_CYCLE */   0x00,
    /* KEY_BRIGHTNESS_AUTO */    0x00,
    /* KEY_DISPLAY_OFF */        0x00,
    /* KEY_WWAN */               0x00,
    /* KEY_RFKILL */             0x00,
    /* KEY_MICMUTE */            0x00
};

static UCHAR
HIDKeyboardEventCodeToUsageCode(
    USHORT uEventCode)
{
    if (uEventCode < sizeof(ReportCodeToUsageCodeTable))
    {
        return ReportCodeToUsageCodeTable[uEventCode];
    }
    return 0;
}

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
        while (DecodeNextBit(&pKeys->u.bitmap[i], &uValue))
        {
            USHORT uKeyCode = uValue + 8 * i;
            UCHAR uCode = HIDKeyboardEventCodeToUsageCode(uKeyCode);
            if (uCode != 0x00)
            {
                bGotKey = TRUE;
                if (uCode < 0xE0 || uCode > 0xE7)
                {
                    uMaxKey = max(uMaxKey, uCode);
                }
            }
        }
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
                uMaxLed + 1);

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
        UCHAR uCode = HIDKeyboardEventCodeToUsageCode(pEvent->code);
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
        else if (uCode != 0x00)
        {
            HIDKeyboardEventKeyToReportKey(pKeyboardDesc, uCode, pEvent->value);
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
