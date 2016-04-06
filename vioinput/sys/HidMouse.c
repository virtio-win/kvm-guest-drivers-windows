/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: HidMouse.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Mouse specific HID functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "HidMouse.tmh"
#endif

static VOID
HIDMouseAxisMapAppend(
    PDYNAMIC_ARRAY pAxisMap,
    ULONG uRelCode,
    ULONG uAxisIndex)
{
    DynamicArrayAppend(pAxisMap, &uRelCode, sizeof(ULONG));
    DynamicArrayAppend(pAxisMap, &uAxisIndex, sizeof(ULONG));
}

static VOID
HIDMouseDescribeWheel(
    PDYNAMIC_ARRAY pHidDesc,
    ULONG uUsagePage,
    ULONG uWheelType)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_LOGICAL);

    // resolution multiplier feature (appears to be mandatory for wheel support)
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_RESOLUTION_MULTIPLIER);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MINIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MAXIMUM, 0x04);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x02);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_FEATURE, HID_DATA_FLAG_VARIABLE);

    // the wheel itself
    if (uUsagePage != 0)
    {
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, uUsagePage);
    }
    HIDAppend2(pHidDesc, HID_TAG_USAGE, uWheelType);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x81);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x7F);
    HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MAXIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x08);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_RELATIVE);

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
HIDMouseBuildReportDescriptor(
    PDYNAMIC_ARRAY pHidDesc,
    PINPUT_CLASS_MOUSE pMouseDesc,
    PVIRTIO_INPUT_CFG_DATA pAxes,
    PVIRTIO_INPUT_CFG_DATA pButtons)
{
    UCHAR i, uValue;
    ULONG uFeatureBitsUsed = 0;
    SIZE_T cbFeatureBytes = 0, cbButtonBytes;
    DYNAMIC_ARRAY AxisMap = { NULL };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_MOUSE);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_POINTER);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_PHYSICAL);

    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pMouseDesc->Common.uReportID);

    for (i = 0; i < pButtons->size; i++)
    {
        unsigned char non_buttons = 0;
        while (DecodeNextBit(&pButtons->u.bitmap[i], &uValue))
        {
            ULONG uButtonCode = uValue + 8 * i;
            if (uButtonCode >= BTN_MOUSE && uButtonCode < BTN_JOYSTICK)
            {
                // individual mouse button functions are not specified in the HID report,
                // only their count is; the max button code we find will determine the count
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got button %d\n", uButtonCode);
                pMouseDesc->uNumOfButtons = max(pMouseDesc->uNumOfButtons, uButtonCode - BTN_MOUSE + 1);
            }
            else if (uButtonCode == BTN_GEAR_DOWN || uButtonCode == BTN_GEAR_UP) 
            {
                // wheel is modeled as a pair of buttons in evdev but it's an axis in HID
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got button-based vertical wheel\n");
                pMouseDesc->uFlags |= CLASS_MOUSE_HAS_V_WHEEL;
            }
            else
            {
                // not a mouse button, put it back and let other devices claim it
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got non-button key %d\n", uButtonCode);
                non_buttons |= (1 << uValue);
            }
        }
        pButtons->u.bitmap[i] = non_buttons;
    }

    cbButtonBytes = (pMouseDesc->uNumOfButtons + 7) / 8;
    pMouseDesc->cbAxisOffset = HID_REPORT_DATA_OFFSET + cbButtonBytes;
    if (pMouseDesc->uNumOfButtons > 0)
    {
        // one bit per button as usual in a mouse device
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_BUTTON);
        HIDAppend2(pHidDesc, HID_TAG_USAGE_MINIMUM, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_USAGE_MAXIMUM, pMouseDesc->uNumOfButtons);
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, pMouseDesc->uNumOfButtons);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

        // pad to the nearest whole byte
        if (pMouseDesc->uNumOfButtons % 8)
        {
            HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
            HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, (ULONG)(cbButtonBytes * 8) - pMouseDesc->uNumOfButtons);
            HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);
        }
    }

    if (pAxes->size > 0)
    {
        // describe relative axes
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);

        DynamicArrayReserve(&AxisMap, 5 * sizeof(ULONG));
        for (i = 0; i < pAxes->size; i++)
        {
            while (DecodeNextBit(&pAxes->u.bitmap[i], &uValue))
            {
                ULONG uRelCode = uValue + 8 * i;
                ULONG uAxisCode = 0;
                switch (uRelCode)
                {
                case REL_X: uAxisCode = HID_USAGE_GENERIC_X; break;
                case REL_Y: uAxisCode = HID_USAGE_GENERIC_Y; break;
                case REL_Z: uAxisCode = HID_USAGE_GENERIC_Z; break;
                case REL_RX: uAxisCode = HID_USAGE_GENERIC_RX; break;
                case REL_RY: uAxisCode = HID_USAGE_GENERIC_RY; break;
                case REL_RZ: uAxisCode = HID_USAGE_GENERIC_RZ; break;
                case REL_WHEEL:
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got axis-based vertical wheel\n");
                    pMouseDesc->uFlags |= CLASS_MOUSE_SUPPORTS_REL_WHEEL;
                    pMouseDesc->uFlags |= CLASS_MOUSE_HAS_V_WHEEL;
                    break;
                }
                case REL_DIAL:
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got axis-based horizontal wheel (REL_DIAL)\n");
                    pMouseDesc->uFlags |= CLASS_MOUSE_SUPPORTS_REL_DIAL;
                    pMouseDesc->uFlags |= CLASS_MOUSE_HAS_H_WHEEL;
                    break;
                }
                case REL_HWHEEL:
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got axis-based horizontal wheel (REL_HWHEEL)\n");
                    pMouseDesc->uFlags |= CLASS_MOUSE_SUPPORTS_REL_HWHEEL;
                    pMouseDesc->uFlags |= CLASS_MOUSE_HAS_H_WHEEL;
                    break;
                }
                default: uAxisCode = HID_USAGE_GENERIC_SLIDER; break;
                }

                if (uAxisCode != 0)
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got axis %d\n", uAxisCode);
                    HIDAppend2(pHidDesc, HID_TAG_USAGE, uAxisCode);

                    // add mapping for this axis
                    HIDMouseAxisMapAppend(&AxisMap, uRelCode, pMouseDesc->uNumOfAxes);
                    pMouseDesc->uNumOfAxes++;
                }
            }
        }

        // if we have detected axis-based wheel support, add mapping for those too
        if (pMouseDesc->uFlags & CLASS_MOUSE_SUPPORTS_REL_WHEEL)
        {
            HIDMouseAxisMapAppend(&AxisMap, REL_WHEEL, pMouseDesc->uNumOfAxes);
        }
        if ((pMouseDesc->uFlags & CLASS_MOUSE_SUPPORTS_REL_HWHEEL) ||
            (pMouseDesc->uFlags & CLASS_MOUSE_SUPPORTS_REL_DIAL))
        {
            ULONG uOffset = pMouseDesc->uNumOfAxes;
            if (pMouseDesc->uFlags & CLASS_MOUSE_HAS_V_WHEEL)
            {
                // horizontal wheel field follows the vertical wheel field
                uOffset++;
            }
            if (pMouseDesc->uFlags & CLASS_MOUSE_SUPPORTS_REL_HWHEEL)
            {
                HIDMouseAxisMapAppend(&AxisMap, REL_HWHEEL, uOffset);
            }
            if (pMouseDesc->uFlags & CLASS_MOUSE_SUPPORTS_REL_DIAL)
            {
                HIDMouseAxisMapAppend(&AxisMap, REL_DIAL, uOffset);
            }
        }

        // finalize the axis map
        HIDMouseAxisMapAppend(&AxisMap, (ULONG)-1, (ULONG)-1);
        pMouseDesc->pAxisMap = DynamicArrayGet(&AxisMap, NULL);
        if (pMouseDesc->pAxisMap == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (pMouseDesc->uNumOfAxes > 0)
        {
            // the range is -127 to 127 (one byte) on all axes
            HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x81);
            HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x7f);

            HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x08);
            HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, pMouseDesc->uNumOfAxes);

            HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_RELATIVE);
        }
    }

    if (pMouseDesc->uFlags & CLASS_MOUSE_HAS_V_WHEEL)
    {
        // vertical mouse wheel
        HIDMouseDescribeWheel(pHidDesc, 0, HID_USAGE_GENERIC_WHEEL);
        uFeatureBitsUsed += 2;
    }

    if (pMouseDesc->uFlags & CLASS_MOUSE_HAS_H_WHEEL)
    {
        // horizontal mouse wheel
        HIDMouseDescribeWheel(pHidDesc, HID_USAGE_PAGE_CONSUMER, HID_USAGE_CONSUMER_AC_PAN);
        uFeatureBitsUsed += 2;
    }

    // feature padding
    if (uFeatureBitsUsed % 8)
    {
        cbFeatureBytes = (uFeatureBitsUsed + 7) / 8;
        HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MINIMUM, 0x00);
        HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MAXIMUM, 0x00);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, (ULONG)(cbFeatureBytes * 8) - uFeatureBitsUsed);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_FEATURE, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);
    }

    // close all collections
    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);
    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Created HID mouse report descriptor with %d buttons, %d axes, vwheel %s, hwheel %s\n",
                pMouseDesc->uNumOfButtons,
                pMouseDesc->uNumOfAxes,
                (pMouseDesc->uFlags & CLASS_MOUSE_HAS_V_WHEEL) ? "YES" : "NO",
                (pMouseDesc->uFlags & CLASS_MOUSE_HAS_H_WHEEL) ? "YES" : "NO");

    // set the total mouse HID report size
    pMouseDesc->Common.cbHidReportSize =
        pMouseDesc->cbAxisOffset +
        pMouseDesc->uNumOfAxes +
        ((pMouseDesc->uFlags & CLASS_MOUSE_HAS_V_WHEEL) ? 1 : 0) +
        ((pMouseDesc->uFlags & CLASS_MOUSE_HAS_H_WHEEL) ? 1 : 0);

    // allocate and initialize the mouse HID report
    pMouseDesc->Common.pHidReport = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pMouseDesc->Common.cbHidReportSize,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pMouseDesc->Common.pHidReport == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pMouseDesc->Common.pHidReport, pMouseDesc->Common.cbHidReportSize);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
HIDMouseReleaseClass(
    PINPUT_CLASS_MOUSE pMouseDesc)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (pMouseDesc->Common.pHidReport != NULL)
    {
        ExFreePoolWithTag(pMouseDesc->Common.pHidReport, VIOINPUT_DRIVER_MEMORY_TAG);
        pMouseDesc->Common.pHidReport = NULL;
    }
    if (pMouseDesc->pAxisMap != NULL)
    {
        ExFreePoolWithTag(pMouseDesc->pAxisMap, VIOINPUT_DRIVER_MEMORY_TAG);
        pMouseDesc->pAxisMap = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

VOID
HIDMouseEventToReport(
    PINPUT_CLASS_MOUSE pMouseDesc,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pMouseDesc->Common.pHidReport;
    SIZE_T i;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pMouseDesc->Common.uReportID;
    switch (pEvent->type)
    {
    case EV_REL:
        if (pMouseDesc->pAxisMap)
        {
            PULONG pMap;
            for (pMap = pMouseDesc->pAxisMap; pMap[0] != (ULONG)-1; pMap += 2)
            {
                // axis map handles regular relative axes as well as wheels
                if (pMap[0] == pEvent->code)
                {
                    pReport[pMouseDesc->cbAxisOffset + pMap[1]] = (UCHAR)pEvent->value;
                    pMouseDesc->Common.bDirty = TRUE;
                    break;
                }
            }
        }
        break;

    case EV_KEY:
        if (pEvent->code == BTN_GEAR_DOWN || pEvent->code == BTN_GEAR_UP)
        {
            if (pEvent->value && (pMouseDesc->uFlags & CLASS_MOUSE_HAS_V_WHEEL))
            {
                // increment/decrement the vertical wheel field
                CHAR delta = (pEvent->code == BTN_GEAR_DOWN ? -1 : 1);
                pReport[pMouseDesc->cbAxisOffset + pMouseDesc->uNumOfAxes] += delta;
                pMouseDesc->Common.bDirty = TRUE;
            }
        }
        else if (pEvent->code >= BTN_MOUSE && pEvent->code < BTN_JOYSTICK)
        {
            USHORT uButton = pEvent->code - BTN_MOUSE;
            if (uButton < pMouseDesc->uNumOfButtons)
            {
                USHORT uOffset = uButton / 8;
                UCHAR uBit = 1 << (uButton % 8);
                if (pEvent->value)
                {
                    pReport[HID_REPORT_DATA_OFFSET + uOffset] |= uBit;
                }
                else
                {
                    pReport[HID_REPORT_DATA_OFFSET + uOffset] &= ~uBit;
                }
                pMouseDesc->Common.bDirty = TRUE;
            }
        }
        break;

    case EV_SYN:
        // reset all axes and wheel to 0, buttons stay unchanged
        for (i = pMouseDesc->cbAxisOffset; i < pMouseDesc->Common.cbHidReportSize; i++)
        {
            pReport[i] = 0;
        }
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
}
