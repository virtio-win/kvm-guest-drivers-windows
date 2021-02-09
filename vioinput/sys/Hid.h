/*
 * HID related definitions
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

#ifndef HID_USAGE_LED_SYSTEM_SUSPEND
#define HID_USAGE_LED_SYSTEM_SUSPEND           0x4C
#endif
#define HID_USAGE_LED_EXTERNAL_POWER_CONNECTED 0x4D

#define HID_USAGE_DIGITIZER                    0x01
#define HID_USAGE_PEN                          0x02
#define HID_USAGE_TOUCH_SCREEN                 0x04
#define HID_USAGE_TOUCH_PAD                    0x05
#ifndef HID_USAGE_DIGITIZER_STYLUS
#define HID_USAGE_DIGITIZER_STYLUS             0x20
#endif
#ifndef HID_USAGE_DIGITIZER_FINGER
#define HID_USAGE_DIGITIZER_FINGER             0x22
#endif
#define HID_USAGE_DIGITIZER_CONTACT_WIDTH      0x48
#define HID_USAGE_DIGITIZER_CONTACT_HEIGHT     0x49
#define HID_USAGE_DIGITIZER_CONTACT_ID         0x51
#define HID_USAGE_DIGITIZER_CONTACT_COUNT      0x54
#define HID_USAGE_DIGITIZER_CONTACT_COUNT_MAX  0x55
#define HID_USAGE_DIGITIZER_SCAN_TIME          0x56
#define HID_USAGE_DIGITIZER_DATA_VALID_FINGER  0x37

#ifndef HID_USAGE_SIMULATION_BRAKE
#define HID_USAGE_SIMULATION_BRAKE             0xC5
#endif
#ifndef HID_USAGE_SIMULATION_STEERING
#define HID_USAGE_SIMULATION_STEERING          0xC8
#endif
#define HID_USAGE_SIMULATION_ACCELERATOR       0xC4

#define KEY_USAGE_MASK    0x0000FFFF
#define KEY_TYPE_MASK     0xFFFF0000
#define KEY_TYPE_KEYBOARD 0x00000000
#define KEY_TYPE_CONSUMER (HID_USAGE_PAGE_CONSUMER << 16)

ULONG
HIDKeyboardEventCodeToUsageCode(
    USHORT uEventCode
);
