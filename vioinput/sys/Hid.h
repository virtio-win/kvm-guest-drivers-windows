/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: Hid.h
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * HID related definitions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#pragma once

#define HID_TAG_USAGE_PAGE         0x04
#define HID_TAG_USAGE              0x08
#define HID_TAG_LOGICAL_MINIMUM    0x14
#define HID_TAG_USAGE_MINIMUM      0x18
#define HID_TAG_LOGICAL_MAXIMUM    0x24
#define HID_TAG_USAGE_MAXIMUM      0x28
#define HID_TAG_PHYSICAL_MINIMUM   0x34
#define HID_TAG_PHYSICAL_MAXIMUM   0x44
#define HID_TAG_UNIT_EXPONENT      0x54
#define HID_TAG_UNIT               0x64
#define HID_TAG_REPORT_SIZE        0x74
#define HID_TAG_INPUT              0x80
#define HID_TAG_REPORT_ID          0x84
#define HID_TAG_OUTPUT             0x90
#define HID_TAG_REPORT_COUNT       0x94
#define HID_TAG_COLLECTION         0xa0
#define HID_TAG_PUSH               0xa4
#define HID_TAG_FEATURE            0xb0
#define HID_TAG_POP                0xb4
#define HID_TAG_END_COLLECTION     0xc0

#define HID_COLLECTION_PHYSICAL    0x00
#define HID_COLLECTION_APPLICATION 0x01
#define HID_COLLECTION_LOGICAL     0x02

#define HID_DATA_FLAG_CONSTANT     0x01
#define HID_DATA_FLAG_VARIABLE     0x02
#define HID_DATA_FLAG_RELATIVE     0x04
#define HID_DATA_FLAG_WRAP         0x08
#define HID_DATA_FLAG_NON_LINEAR   0x10
#define HID_DATA_FLAG_NO_PREFERRED 0x20
#define HID_DATA_FLAG_NULL_STATE   0x40
#define HID_DATA_FLAG_VOLATILE     0x80

#define HID_USAGE_LED_SYSTEM_SUSPEND           0x4C
#define HID_USAGE_LED_EXTERNAL_POWER_CONNECTED 0x4D

#define HID_USAGE_DIGITIZER                    0x01
#define HID_USAGE_PEN                          0x02
#define HID_USAGE_TOUCH_SCREEN                 0x04
#define HID_USAGE_TOUCH_PAD                    0x05
#define HID_USAGE_DIGITIZER_STYLUS             0x20
#define HID_USAGE_DIGITIZER_CONTACT_ID         0x51
#define HID_USAGE_DIGITIZER_DATA_VALID_FINGER  0x37

#define HID_USAGE_SIMULATION_BRAKE             0xC5
#define HID_USAGE_SIMULATION_STEERING          0xC8
#define HID_USAGE_SIMULATION_ACCELERATOR       0xC4

#define KEY_USAGE_MASK    0x0000FFFF
#define KEY_TYPE_MASK     0xFFFF0000
#define KEY_TYPE_KEYBOARD 0x00000000
#define KEY_TYPE_CONSUMER (HID_USAGE_PAGE_CONSUMER << 16)

ULONG
HIDKeyboardEventCodeToUsageCode(
    USHORT uEventCode
);
