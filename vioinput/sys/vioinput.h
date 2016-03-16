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

// Simple mouse HID report structure
typedef struct _tagMouseHidReport
{
    unsigned char buttons;
    signed char x_axis;
    signed char y_axis;
} MOUSE_HID_REPORT, *PMOUSE_HID_REPORT;

typedef struct _tagInputDevice
{
    VIRTIO_WDF_DRIVER     VDevice;

    WDFINTERRUPT          QueuesInterrupt;

    struct virtqueue      *EventQ;
    struct virtqueue      *StatusQ;

    WDFSPINLOCK           EventQLock;
    WDFSPINLOCK           StatusQLock;

    WDFQUEUE              IoctlQueue;
    WDFQUEUE              HidQueue;

    HID_DEVICE_ATTRIBUTES HidDeviceAttributes;

    MOUSE_HID_REPORT      HidReport;
} INPUT_DEVICE, *PINPUT_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(INPUT_DEVICE, GetDeviceContext)

#define VIOINPUT_DRIVER_MEMORY_TAG (ULONG)'niIV'

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

struct virtio_input_config
{
    unsigned char select;
    unsigned char subsel;
    unsigned char size;
    unsigned char reserved[5];
    union
    {
        char string[128];
        unsigned char bitmap[128];
        struct virtio_input_absinfo abs;
        struct virtio_input_devids ids;
    } u;
};

typedef struct virtio_input_event
{
    unsigned short type;
    unsigned short code;
    unsigned long value;
} VIRTIO_INPUT_EVENT, *PVIRTIO_INPUT_EVENT;

// Event types
#define EV_SYN       0x00
#define EV_KEY       0x01
#define EV_REL       0x02

// Button codes
#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112
#define BTN_SIDE     0x113
#define BTN_EXTRA    0x114
#define BTN_FORWARD  0x115
#define BTN_BACK     0x116
#define BTN_TASK     0x117

// Relative axis codes
#define REL_X        0x00
#define REL_Y        0x01
#define REL_Z        0x02
#define REL_RX       0x03
#define REL_RY       0x04
#define REL_RZ       0x05
#define REL_HWHEEL   0x06
#define REL_DIAL     0x07
#define REL_WHEEL    0x08
#define REL_MISC     0x09

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
