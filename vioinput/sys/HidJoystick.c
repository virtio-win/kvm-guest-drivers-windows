/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: HidJoystick.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Joystick specific HID functionality
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"
#include "Hid.h"

#if defined(EVENT_TRACING)
#include "HidJoystick.tmh"
#endif

// initial length of the axis map (will grow as needed)
#define AXIS_MAP_INITIAL_LENGTH 6

static VOID
HIDJoystickAxisMapAppend(
    PDYNAMIC_ARRAY pAxisMap,
    ULONG uCode,
    SIZE_T uAxisIndex)
{
    DynamicArrayAppend(pAxisMap, &uCode, sizeof(ULONG));
    DynamicArrayAppend(pAxisMap, &uAxisIndex, sizeof(ULONG));
}

NTSTATUS
HIDJoystickBuildReportDescriptor(
    PINPUT_DEVICE pContext,
    PDYNAMIC_ARRAY pHidDesc,
    PINPUT_CLASS_JOYSTICK pJoystickDesc,
    PVIRTIO_INPUT_CFG_DATA pAxes,
    PVIRTIO_INPUT_CFG_DATA pButtons)
{
    UCHAR i, uValue;
    SIZE_T cbButtonBytes;
    ULONG uNumOfAbsAxes = 0;
    DYNAMIC_ARRAY AxisMap = { NULL };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    for (i = 0; i < pButtons->size; i++)
    {
        UCHAR uNonButtons = 0;
        while (DecodeNextBit(&pButtons->u.bitmap[i], &uValue))
        {
            ULONG uButtonCode = uValue + 8 * i;
            if (uButtonCode >= BTN_JOYSTICK && uButtonCode < BTN_GAMEPAD)
            {
                // individual joystick button functions are not specified in the HID report,
                // only their count is; the max button code we find will determine the count
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got button %d\n", uButtonCode);
                pJoystickDesc->uNumOfButtons = max(pJoystickDesc->uNumOfButtons, uButtonCode - BTN_JOYSTICK + 1);
            }
            else
            {
                // not a joystick button, put it back and let other devices claim it
                uNonButtons |= (1 << uValue);
            }
        }
        pButtons->u.bitmap[i] = uNonButtons;
    }

    if (pJoystickDesc->uNumOfButtons == 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Joystick buttons not found\n");
        return STATUS_SUCCESS;
    }

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_GENERIC_JOYSTICK);
    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);

    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pJoystickDesc->Common.uReportID);

    // describe axes
    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
    DynamicArrayReserve(&AxisMap, AXIS_MAP_INITIAL_LENGTH * 2 * sizeof(ULONG));

    for (i = 0; i < pAxes->size; i++)
    {
        UCHAR uNonAxes = 0;
        while (DecodeNextBit(&pAxes->u.bitmap[i], &uValue))
        {
            USHORT uAbsCode = uValue + 8 * i;
            BOOLEAN bSimulationPage = FALSE;
            ULONG uAxisCode = 0;

            switch (uAbsCode)
            {
            case ABS_X: uAxisCode = HID_USAGE_GENERIC_X; break;
            case ABS_Y: uAxisCode = HID_USAGE_GENERIC_Y; break;
            case ABS_Z: uAxisCode = HID_USAGE_GENERIC_Z; break;
            case ABS_RX: uAxisCode = HID_USAGE_GENERIC_RX; break;
            case ABS_RY: uAxisCode = HID_USAGE_GENERIC_RY; break;
            case ABS_RZ: uAxisCode = HID_USAGE_GENERIC_RZ; break;
            case ABS_TILT_X: uAxisCode = HID_USAGE_GENERIC_VX; break;
            case ABS_TILT_Y: uAxisCode = HID_USAGE_GENERIC_VY; break;
            case ABS_MISC: uAxisCode = HID_USAGE_GENERIC_SLIDER; break;
            case ABS_THROTTLE: bSimulationPage = TRUE; uAxisCode = HID_USAGE_SIMULATION_THROTTLE; break;
            case ABS_RUDDER:   bSimulationPage = TRUE; uAxisCode = HID_USAGE_SIMULATION_RUDDER; break;
            case ABS_BRAKE:    bSimulationPage = TRUE; uAxisCode = HID_USAGE_SIMULATION_BRAKE; break;
            case ABS_WHEEL:    bSimulationPage = TRUE; uAxisCode = HID_USAGE_SIMULATION_STEERING; break;
            case ABS_GAS:      bSimulationPage = TRUE; uAxisCode = HID_USAGE_SIMULATION_ACCELERATOR; break;
            }

            if (uAxisCode != 0)
            {
                struct virtio_input_absinfo AbsInfo;
                GetAbsAxisInfo(pContext, uAbsCode, &AbsInfo);

                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got joystick axis %d, min %d, max %d\n",
                            uAxisCode, AbsInfo.min, AbsInfo.max);

                // some of the supported axes are on the generic page, some on the simulation page
                if (bSimulationPage)
                {
                    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_SIMULATION);
                }
                else
                {
                    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
                }
                HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, AbsInfo.min);
                HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, AbsInfo.max);

                // for simplicity always use 4-byte fields in the report
                HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x20);
                HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);

                HIDAppend2(pHidDesc, HID_TAG_USAGE, uAxisCode);
                HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

                // add mapping for this axis
                HIDJoystickAxisMapAppend(&AxisMap, (EV_ABS << 16) | uAbsCode, pJoystickDesc->cbAxisLen);
                pJoystickDesc->cbAxisLen += 4;
                uNumOfAbsAxes++;
            }
            else
            {
                uNonAxes |= (1 << uValue);
            }
        }
        pAxes->u.bitmap[i] = uNonAxes;
    }

    // finalize the axis map
    HIDJoystickAxisMapAppend(&AxisMap, (ULONG)-1, (ULONG)-1);
    pJoystickDesc->pAxisMap = DynamicArrayGet(&AxisMap, NULL);
    if (pJoystickDesc->pAxisMap == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // one bit per button
    cbButtonBytes = (pJoystickDesc->uNumOfButtons + 7) / 8;
    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_BUTTON);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MINIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_USAGE_MAXIMUM, pJoystickDesc->uNumOfButtons);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, pJoystickDesc->uNumOfButtons);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // pad to the nearest whole byte
    if (pJoystickDesc->uNumOfButtons % 8)
    {
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, (ULONG)(cbButtonBytes * 8) - pJoystickDesc->uNumOfButtons);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);
    }

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Created HID joystick report descriptor with %d axes and %d buttons\n",
                uNumOfAbsAxes,
                pJoystickDesc->uNumOfButtons);

    pJoystickDesc->Common.cbHidReportSize =
        1 +                 // report ID
        uNumOfAbsAxes * 4 + // axes
        cbButtonBytes;      // buttons

    // allocate and initialize the joystick HID report
    pJoystickDesc->Common.pHidReport = (PUCHAR)ExAllocatePoolWithTag(
        NonPagedPool,
        pJoystickDesc->Common.cbHidReportSize,
        VIOINPUT_DRIVER_MEMORY_TAG);
    if (pJoystickDesc->Common.pHidReport == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pJoystickDesc->Common.pHidReport, pJoystickDesc->Common.cbHidReportSize);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
HIDJoystickReleaseClass(
    PINPUT_CLASS_JOYSTICK pJoystickDesc)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (pJoystickDesc->Common.pHidReport != NULL)
    {
        ExFreePoolWithTag(pJoystickDesc->Common.pHidReport, VIOINPUT_DRIVER_MEMORY_TAG);
        pJoystickDesc->Common.pHidReport = NULL;
    }
    if (pJoystickDesc->pAxisMap != NULL)
    {
        ExFreePoolWithTag(pJoystickDesc->pAxisMap, VIOINPUT_DRIVER_MEMORY_TAG);
        pJoystickDesc->pAxisMap = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

VOID
HIDJoystickEventToReport(
    PINPUT_CLASS_JOYSTICK pJoystickDesc,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PUCHAR pReport = pJoystickDesc->Common.pHidReport;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pJoystickDesc->Common.uReportID;
    switch (pEvent->type)
    {
    case EV_ABS:
        if (pJoystickDesc->pAxisMap)
        {
            PULONG pMap;
            for (pMap = pJoystickDesc->pAxisMap; pMap[0] != (ULONG)-1; pMap += 2)
            {
                if (pMap[0] == ((pEvent->type << 16) | pEvent->code))
                {
                    // 4 bytes per absolute axis
                    PULONG pAxisPtr = (PULONG)&pReport[HID_REPORT_DATA_OFFSET + pMap[1]];
                    *pAxisPtr = pEvent->value;
                    pJoystickDesc->Common.bDirty = TRUE;
                }
            }
        }
        break;
    case EV_KEY:
        if (pEvent->code >= BTN_JOYSTICK && pEvent->code < BTN_GAMEPAD)
        {
            USHORT uButton = pEvent->code - BTN_JOYSTICK;
            if (uButton < pJoystickDesc->uNumOfButtons)
            {
                SIZE_T cbOffset = pJoystickDesc->cbAxisLen + (uButton / 8);
                UCHAR uBit = 1 << (uButton % 8);
                if (pEvent->value)
                {
                    pReport[HID_REPORT_DATA_OFFSET + cbOffset] |= uBit;
                }
                else
                {
                    pReport[HID_REPORT_DATA_OFFSET + cbOffset] &= ~uBit;
                }
                pJoystickDesc->Common.bDirty = TRUE;
            }
        }
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}
