/*
 * Tablet specific HID functionality
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
#include "HidTablet.tmh"
#endif

#pragma pack(push,1)
typedef struct _tagInputClassTabletSlot
{
    UCHAR uFlags;
    USHORT uContactID;
    USHORT uAxisX;
    USHORT uAxisY;
} INPUT_CLASS_TABLET_SLOT, * PINPUT_CLASS_TABLET_SLOT;

typedef struct _tagInputClassTabletFeatureMaxContact
{
    UCHAR uReportID;
    UCHAR uMaxContacts;
}INPUT_CLASS_TABLET_FEATURE_MAX_CONTACT, * PINPUT_CLASS_TABLET_FEATURE_MAX_CONTACT;
#pragma pack(pop)

typedef struct _tagInputClassTablet
{
    INPUT_CLASS_COMMON Common;
    ULONG uMaxContacts;
    ULONG uLastMTSlot;

    /*
     * HID Tablet report layout:
     * Total size in bytes: 1 + 7 * numContacts + 1
     * +---------------+-+-+-+-+-+---------------+----------+------------+
     * | Byte Offset   |7|6|5|4|3|     2         |    1     |     0      |
     * +---------------+-+-+-+-+-+---------------+----------+------------+
     * | 0             |                    Report ID                    |
     * | i*7+1         |   Pad   | Barrel Switch | In-range | Tip Switch |
     * | i*7+[2,3]     |                    Contact ID                   |
     * | i*7+[4,5]     |                      x-axis                     |
     * | i*7+[6,7]     |                      y-axis                     |
     * | (i+1)*7+[1,7] |                   Contact i+1                   |
     * | (i+2)*7+[1,7] |                   Contact i+2                   |
     * | ...           |                                                 |
     * | (n-1)*7+[1,7] |                   Contact n-1                   |
     * | n*7+1         |                  Contact Count                  |
     * +---------------+-+-+-+-+-+------------+----------+---------------+
     */
} INPUT_CLASS_TABLET, *PINPUT_CLASS_TABLET;

static NTSTATUS
HIDTabletGetFeature(
    PINPUT_CLASS_COMMON pClass,
    PHID_XFER_PACKET pFeaturePkt)
{
    PINPUT_CLASS_TABLET pTabletDesc = (PINPUT_CLASS_TABLET)pClass;
    UCHAR uReportID = *(PUCHAR)pFeaturePkt->reportBuffer;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    switch (uReportID)
    {
    case REPORTID_FEATURE_TABLET_MAX_COUNT:
        if (pFeaturePkt->reportBufferLen >= sizeof(INPUT_CLASS_TABLET_FEATURE_MAX_CONTACT))
        {
            PINPUT_CLASS_TABLET_FEATURE_MAX_CONTACT pFtrReport = (PINPUT_CLASS_TABLET_FEATURE_MAX_CONTACT)pFeaturePkt->reportBuffer;

            pFtrReport->uMaxContacts = (UCHAR)pTabletDesc->uMaxContacts;
        } else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return status;
}

static NTSTATUS
HIDTabletEventToReport(
    PINPUT_CLASS_COMMON pClass,
    PVIRTIO_INPUT_EVENT pEvent)
{
    PINPUT_CLASS_TABLET pTabletDesc = (PINPUT_CLASS_TABLET)pClass;
    PUCHAR pReport = pClass->pHidReport;
    PUSHORT pAxisReport;
    PINPUT_CLASS_TABLET_SLOT pReportSlot;
    UCHAR uBits;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pReport[HID_REPORT_ID_OFFSET] = pClass->uReportID;
    switch (pEvent->type)
    {
    case EV_ABS:
        /*
         * ST always fills ABS_X/ABS_Y and EV_KEY in 1st slot while
         * uLastMTSlot remains unchanged for all events.
         */
        pReportSlot = &((PINPUT_CLASS_TABLET_SLOT)&pReport[HID_REPORT_DATA_OFFSET])[pTabletDesc->uLastMTSlot];
        switch (pEvent->code)
        {
        case ABS_X:
        case ABS_Y:
            {
                USHORT* pPos = (pEvent->code == ABS_X ? &pReportSlot->uAxisX : &pReportSlot->uAxisY);

                *pPos = (USHORT)pEvent->value;
                pClass->bDirty = TRUE;
            }
            break;
        default:
            break;
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
            pReportSlot = &((PINPUT_CLASS_TABLET_SLOT)&pReport[HID_REPORT_DATA_OFFSET])[pTabletDesc->uLastMTSlot];
            if (pEvent->value)
            {
                pReportSlot->uFlags |= uBits;
            }
            else
            {
                pReportSlot->uFlags &= ~uBits;
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
    ULONG uAxisCode, uNumOfAbsAxes = 0, uNumContacts = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // allocate and initialize pTabletDesc
    pTabletDesc = VIOInputAlloc(sizeof(INPUT_CLASS_TABLET));
    if (pTabletDesc == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    pTabletDesc->uMaxContacts = 1;
    pTabletDesc->uLastMTSlot = 0;

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
        status = STATUS_INSUFFICIENT_RESOURCES;
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

    pTabletDesc->Common.GetFeatureFunc = HIDTabletGetFeature;
    pTabletDesc->Common.EventToReportFunc = HIDTabletEventToReport;
    pTabletDesc->Common.uReportID = (UCHAR)(pContext->uNumOfClasses + 1);

    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_DIGITIZER);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER);

    HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_APPLICATION);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, pTabletDesc->Common.uReportID);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_STYLUS);

    for (uNumContacts = 0; uNumContacts < pTabletDesc->uMaxContacts; uNumContacts++)
    {
        // Collection Logical for all contacts for reporting in hybrid mode
        HIDAppend2(pHidDesc, HID_TAG_COLLECTION, HID_COLLECTION_LOGICAL);

        // Change to digitizer page for touch
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_DIGITIZER);

        // Same logical minimum and maximum applied to below flags, 1 bit each, 1 byte total
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);

        // tip switch, one bit
        HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_TIP_SWITCH);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

        // in range flag, one bit
        HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_IN_RANGE);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

        // barrel switch, one bit
        HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_BARREL_SWITCH);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

        // padding
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, 0x00);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x05);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);

        // Contact Identifier, 2 bytes
        HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x10);
        HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);
        HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_CONTACT_ID);
        HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, pTabletDesc->uMaxContacts - 1);
        HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

        // Change to generic desktop page for coordinates
        HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_GENERIC);
        HIDAppend2(pHidDesc, HID_TAG_UNIT_EXPONENT, 0x0E); // 10^(-2)
        HIDAppend2(pHidDesc, HID_TAG_UNIT, 0x11);          // Linear Centimeter

        // 2 bytes each axis
        for (uAxisCode = ABS_X; uAxisCode <= ABS_Y; uAxisCode++)
        {
            struct virtio_input_absinfo AbsInfo;
            GetAbsAxisInfo(pContext, uAxisCode, &AbsInfo);

            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Got abs axis %d, min %d, max %d\n",
                uAxisCode, AbsInfo.min, AbsInfo.max);

            // Device Class Definition for HID 1.11, 6.2.2.7
            // Resolution = (Logical Maximum - Logical Minimum) /
            //    ((Physical Maximum - Physical Minimum) * (10 Unit Exponent))
            // struct input_absinfo{}.res reports in units/mm, convert to cm here.
            HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, AbsInfo.min);
            HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, AbsInfo.max);
            HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MINIMUM,
                (AbsInfo.res == 0) ? AbsInfo.min : (AbsInfo.min * 10 / AbsInfo.res));
            HIDAppend2(pHidDesc, HID_TAG_PHYSICAL_MAXIMUM,
                (AbsInfo.res == 0) ? AbsInfo.max : (AbsInfo.max * 10 / AbsInfo.res));

            HIDAppend2(pHidDesc, HID_TAG_USAGE,
                (uAxisCode == ABS_X ? HID_USAGE_GENERIC_X : HID_USAGE_GENERIC_Y));

            HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);
        }

        HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION); //HID_COLLECTION_LOGICAL
    }

    // Change to digitizer page for contacts information
    HIDAppend2(pHidDesc, HID_TAG_USAGE_PAGE, HID_USAGE_PAGE_DIGITIZER);

    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MINIMUM, 0x00);
    HIDAppend2(pHidDesc, HID_TAG_LOGICAL_MAXIMUM, pTabletDesc->uMaxContacts);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_SIZE, 0x08);
    HIDAppend2(pHidDesc, HID_TAG_REPORT_COUNT, 0x01);

    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_CONTACT_COUNT);
    HIDAppend2(pHidDesc, HID_TAG_INPUT, HID_DATA_FLAG_VARIABLE);

    // IOCTL_HID_GET_FEATURE will query the report for maximum count
    HIDAppend2(pHidDesc, HID_TAG_REPORT_ID, REPORTID_FEATURE_TABLET_MAX_COUNT);
    HIDAppend2(pHidDesc, HID_TAG_USAGE, HID_USAGE_DIGITIZER_CONTACT_COUNT_MAX);
    HIDAppend2(pHidDesc, HID_TAG_FEATURE, HID_DATA_FLAG_VARIABLE | HID_DATA_FLAG_CONSTANT);

    HIDAppend1(pHidDesc, HID_TAG_END_COLLECTION); //HID_COLLECTION_APPLICATION

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Created HID tablet report descriptor\n");

    // calculate the tablet HID report size
    pTabletDesc->Common.cbHidReportSize =
        1 + // report ID
        sizeof(INPUT_CLASS_TABLET_SLOT) * pTabletDesc->uMaxContacts + // max contacts * per-contact packet. See INPUT_CLASS_TABLET_SLOT and INPUT_CLASS_TABLET for layout details.
        1 // Actual contact count
        ;

    // register the tablet class
    status = RegisterClass(pContext, &pTabletDesc->Common);
    if (NT_SUCCESS(status))
    {
        PUCHAR pReport = pTabletDesc->Common.pHidReport;

        /*
         * For ST, the number of contacts to report is always 1.
         */
        pReport[HID_REPORT_DATA_OFFSET + sizeof(INPUT_CLASS_TABLET_SLOT) * pTabletDesc->uMaxContacts] = 1;
    }

Exit:
    if (!NT_SUCCESS(status) && pTabletDesc != NULL)
    {
        VIOInputFree(&pTabletDesc);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s (%08x)\n", __FUNCTION__, status);
    return status;
}
