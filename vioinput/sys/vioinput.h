/**********************************************************************
 * Copyright (c) 2010-2015 Red Hat, Inc.
 *
 * File: vioinput.h
 *
 * Author(s):
 *
 * Main include file
 * This file contains various routines and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#pragma once
#include "public.h"

EVT_WDF_DRIVER_DEVICE_ADD VIOInputEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR     VIOInputInterruptIsr;
EVT_WDF_INTERRUPT_DPC     VIOInputQueuesInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE  VIOInputInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE VIOInputInterruptDisable;

typedef UCHAR HID_REPORT_DESCRIPTOR, *PHID_REPORT_DESCRIPTOR;

//
// These are the device attributes returned by the mini driver in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//
#define HIDMINI_PID     0x1052
#define HIDMINI_VID     0x1AF4
#define HIDMINI_VERSION 0x0001

typedef struct _tagInputClassCommon
{
    // offset of this class's data in the HID report
    SIZE_T cbHidReportOffset;
    // size of this class's data in the HID report
    SIZE_T cbHidReportSize;
} INPUT_CLASS_COMMON, *PINPUT_CLASS_COMMON;

typedef struct _tagInputClassMouse
{
    INPUT_CLASS_COMMON Common;

    // the mouse HID report is laid out as follows:
    // offset 0
    // * buttons; one bit per button followed by padding to the nearest whole byte
    // offset cbAxisOffset
    // * axes; one byte per axis, mapping in pAxisMap
    // offset cbAxisOffset + cbNumOfAxes
    // * vertical wheel; one byte, if uFlags & CLASS_MOUSE_HAS_V_WHEEL
    // * horizontal wheel; one byte, if uFlags & CLASS_MOUSE_HAS_H_WHEEL

    // number of buttons supported by the HID report
    ULONG  uNumOfButtons;
    // offset of axis data within the HID report
    SIZE_T cbAxisOffset;
    // number of axes supported by the HID report
    ULONG  uNumOfAxes;
    // mapping from EVDEV axis codes to HID axis indices
    PULONG pAxisMap;
    // flags
#define CLASS_MOUSE_HAS_V_WHEEL         0x01
#define CLASS_MOUSE_HAS_H_WHEEL         0x02
#define CLASS_MOUSE_SUPPORTS_REL_WHEEL  0x04
#define CLASS_MOUSE_SUPPORTS_REL_HWHEEL 0x08
#define CLASS_MOUSE_SUPPORTS_REL_DIAL   0x10
    ULONG  uFlags;
} INPUT_CLASS_MOUSE, *PINPUT_CLASS_MOUSE;

typedef struct _tagInputDevice
{
    VIRTIO_WDF_DRIVER      VDevice;

    WDFINTERRUPT           QueuesInterrupt;

    struct virtqueue       *EventQ;
    struct virtqueue       *StatusQ;

    WDFSPINLOCK            EventQLock;
    WDFSPINLOCK            StatusQLock;

    WDFQUEUE               IoctlQueue;
    WDFQUEUE               HidQueue;

    HID_DESCRIPTOR         HidDescriptor;
    HID_DEVICE_ATTRIBUTES  HidDeviceAttributes;
    PHID_REPORT_DESCRIPTOR HidReportDescriptor;

    PUCHAR                 HidReport;
    SIZE_T                 HidReportSize;

    INPUT_CLASS_MOUSE      MouseDesc;
} INPUT_DEVICE, *PINPUT_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(INPUT_DEVICE, GetDeviceContext)

#define VIOINPUT_DRIVER_MEMORY_TAG (ULONG)'niIV'

typedef struct _tagDynamicArray
{
    PVOID  Ptr;
    SIZE_T Size;
    SIZE_T MaxSize;
} DYNAMIC_ARRAY, *PDYNAMIC_ARRAY;

// Virtio-input data structures:

enum virtio_input_config_select
{
    VIRTIO_INPUT_CFG_UNSET = 0x00,
    VIRTIO_INPUT_CFG_ID_NAME = 0x01,
    VIRTIO_INPUT_CFG_ID_SERIAL = 0x02,
    VIRTIO_INPUT_CFG_ID_DEVIDS = 0x03,
    VIRTIO_INPUT_CFG_PROP_BITS = 0x10,
    VIRTIO_INPUT_CFG_EV_BITS = 0x11,
    VIRTIO_INPUT_CFG_ABS_INFO = 0x12,
};

struct virtio_input_absinfo
{
    unsigned long min;
    unsigned long max;
    unsigned long fuzz;
    unsigned long flat;
    unsigned long res;
};

struct virtio_input_devids
{
    unsigned short bustype;
    unsigned short vendor;
    unsigned short product;
    unsigned short version;
};

union virtio_input_raw_cfg_data
{
    char string[128];
    unsigned char bitmap[128];
    struct virtio_input_absinfo abs;
    struct virtio_input_devids ids;
};

typedef struct virtio_input_cfg_data
{
    unsigned char size;
    union virtio_input_raw_cfg_data u;
} VIRTIO_INPUT_CFG_DATA, *PVIRTIO_INPUT_CFG_DATA;

struct virtio_input_config
{
    unsigned char select;
    unsigned char subsel;
    unsigned char size;
    unsigned char reserved[5];
    union virtio_input_raw_cfg_data u;
};

typedef struct virtio_input_event
{
    unsigned short type;
    unsigned short code;
    unsigned long value;
} VIRTIO_INPUT_EVENT, *PVIRTIO_INPUT_EVENT;

// Event types
#define EV_SYN        0x00
#define EV_KEY        0x01
#define EV_REL        0x02

// Button codes
#define BTN_MOUSE     0x110
#define BTN_LEFT      0x110
#define BTN_RIGHT     0x111
#define BTN_MIDDLE    0x112
#define BTN_SIDE      0x113
#define BTN_EXTRA     0x114
#define BTN_FORWARD   0x115
#define BTN_BACK      0x116
#define BTN_TASK      0x117

#define BTN_JOYSTICK  0x120

#define BTN_WHEEL     0x150
#define BTN_GEAR_DOWN 0x150
#define BTN_GEAR_UP   0x151

// Relative axis codes
#define REL_X         0x00
#define REL_Y         0x01
#define REL_Z         0x02
#define REL_RX        0x03
#define REL_RY        0x04
#define REL_RZ        0x05
#define REL_HWHEEL    0x06
#define REL_DIAL      0x07
#define REL_WHEEL     0x08
#define REL_MISC      0x09

NTSTATUS
VIOInputFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock
);

NTSTATUS
VIOInputAddInBuf(
    IN struct virtqueue *vq,
    IN PVIRTIO_INPUT_EVENT buf
);

NTSTATUS
VIOInputBuildReportDescriptor(
    PINPUT_DEVICE pContext
);

VOID
EvtIoDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode
);

VOID
ProcessInputEvent(
    PINPUT_DEVICE pContext,
    PVIRTIO_INPUT_EVENT pEvent
);

NTSTATUS
RequestCopyFromBuffer(
    WDFREQUEST Request,
    PVOID SourceBuffer,
    size_t NumBytesToCopyFrom
);

BOOLEAN
DynamicArrayReserve(
    PDYNAMIC_ARRAY pArray,
    SIZE_T cbSize
);

BOOLEAN
DynamicArrayAppend(
    PDYNAMIC_ARRAY pArray,
    PVOID pData,
    SIZE_T cbLength
);

VOID
DynamicArrayDestroy(
    PDYNAMIC_ARRAY pArray
);

PVOID
DynamicArrayGet(
    PDYNAMIC_ARRAY pArray,
    SIZE_T *pcbSize
);

VOID
HIDAppend1(
    PDYNAMIC_ARRAY pArray,
    UCHAR tag
);

VOID
HIDAppend2(
    PDYNAMIC_ARRAY pArray,
    UCHAR tag,
    ULONG value
);

BOOLEAN
DecodeNextBit(
    PUCHAR pBitmap,
    PUCHAR pValue
);

NTSTATUS
HIDMouseBuildReportDescriptor(
    PDYNAMIC_ARRAY pHidDesc,
    PINPUT_CLASS_MOUSE pMouseDesc,
    PVIRTIO_INPUT_CFG_DATA pAxes,
    PVIRTIO_INPUT_CFG_DATA pButtons
);

VOID
HIDMouseReleaseClass(
    PINPUT_CLASS_MOUSE pMouseDesc
);

VOID
HIDMouseEventToReport(
    PINPUT_CLASS_MOUSE pMouseDesc,
    PVIRTIO_INPUT_EVENT pEvent,
    PUCHAR pReport
);
