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
#define HID_TAG_REPORT_SIZE        0x74
#define HID_TAG_INPUT              0x80
#define HID_TAG_OUTPUT             0x90
#define HID_TAG_REPORT_COUNT       0x94
#define HID_TAG_COLLECTION         0xa0
#define HID_TAG_FEATURE            0xb0
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
