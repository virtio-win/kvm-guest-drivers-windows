/**********************************************************************
 * Copyright (c) 2010-2015 Red Hat, Inc.
 *
 * File: precomp.h
 *
 * Author(s):
 *
 * Pre-compiled header file for vioinput driver.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include <stddef.h>
#include <stdarg.h>
#include <ntddk.h>
#include <wdf.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions
#include <wdmguid.h> // required for WMILIB_CONTEXT
#include <wmistr.h>
#include <wmilib.h>
#include <ntintsafe.h>
#include <hidport.h>
#include <hidusage.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio_config.h"
#include "virtio.h"
#include "VirtIOWdf.h"

#include "trace.h"
