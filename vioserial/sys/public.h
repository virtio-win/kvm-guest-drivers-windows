/**********************************************************************
 * Copyright (c) 2010-2016 Red Hat, Inc.
 *
 * File: public.h
 *
 * Author(s):
 *
 * Public include file
 * This file contains
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#if !defined(VIOS_PUBLIC_H)
#define VIOS_PUBLIC_H

DEFINE_GUID (GUID_VIOSERIAL_CONTROLLER,
        0xF55F7844, 0x6A0C, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//  {F55F7844-6A0C-11d2-B841-00C04FAD5171}

DEFINE_GUID(GUID_VIOSERIAL_PORT,
0x6fde7521, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7521-1B65-48ae-B628-80BE62016026}

#define IOCTL_GET_INFORMATION    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

typedef struct _tagVirtioPortInfo {
    UINT                Id;
    BOOLEAN             OutVqFull;
    BOOLEAN             HostConnected;
    BOOLEAN             GuestConnected;
    CHAR                Name[1];
}VIRTIO_PORT_INFO, * PVIRTIO_PORT_INFO;

DEFINE_GUID(GUID_VIOSERIAL_PORT_CHANGE_STATUS,
0x2c0f39ac, 0xb156, 0x4237, 0x9c, 0x64, 0x89, 0x91, 0xa1, 0x8b, 0xf3, 0x5c);
// {2C0F39AC-B156-4237-9C64-8991A18BF35C}

typedef struct _tagVirtioPortStatusChange {
    ULONG Version;
    ULONG Reason;
} VIRTIO_PORT_STATUS_CHANGE, *PVIRTIO_PORT_STATUS_CHANGE;

#endif /* VIOS_PUBLIC_H */
