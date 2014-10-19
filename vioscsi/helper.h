/**********************************************************************
 * Copyright (c) 2012  Red Hat, Inc.
 *
 * File: helper.h
 *
 * Author(s):
 * Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Virtio block device include module.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef ___HELPER_H___
#define ___HELPER_H___


#include <ntddk.h>

#include <storport.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "vioscsi.h"

BOOLEAN
SendSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

BOOLEAN
SendTMF(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
ShutDown(
    IN PVOID DeviceExtension
    );

BOOLEAN
DeviceReset(
    IN PVOID DeviceExtension
    );

VOID
GetScsiConfig(
    IN PVOID DeviceExtension
    );

BOOLEAN
InitHW(
    IN PVOID DeviceExtension, 
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo
    );

VOID
LogError(
    IN PVOID HwDeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    );

BOOLEAN
KickEvent(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEventNode event
    );

BOOLEAN
SynchronizedKickEventRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    );
    
#endif // ___HELPER_H___
