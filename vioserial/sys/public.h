/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
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


DEFINE_GUID(GUID_VIOSERIAL_PORT,
0x6fde7521, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7521-1B65-48ae-B628-80BE62016026}

#define IOCTL_GET_INFORMATION    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _tagVirtioPortInfo {
    UINT                Id;
    BOOLEAN             OutVqFull;
    BOOLEAN             HostConnected;
    BOOLEAN             GuestConnected;
    CHAR                Name[1];
}VIRTIO_PORT_INFO, * PVIRTIO_PORT_INFO;

#endif /* VIOS_PUBLIC_H */
