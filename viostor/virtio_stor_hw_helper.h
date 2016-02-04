/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: virtio_stor_hw_helper.h
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
#ifndef ___VIOSTOR_HW_HELPER_H___
#define ___VIOSTOR_HW_HELPER_H___


#include <ntddk.h>

#ifdef USE_STORPORT
#define STOR_USE_SCSI_ALIASES
#include <storport.h>
#else
#include <scsi.h>
#endif

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio_config.h"
#include "virtio.h"
#include "virtio_stor.h"


BOOLEAN
RhelDoReadWrite(
    IN PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb
    );

BOOLEAN
RhelDoFlush(
    IN PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb,
    IN BOOLEAN sync
    );

VOID
RhelShutDown(
    IN PVOID DeviceExtension
    );

ULONGLONG
RhelGetLba(
    IN PVOID DeviceExtension,
    IN PCDB Cdb
    );

VOID
RhelGetSerialNumber(
    IN PVOID DeviceExtension
    );

VOID
RhelGetDiskGeometry(
    IN PVOID DeviceExtension
    );

#endif ___VIOSTOR_HW_HELPER_H___
