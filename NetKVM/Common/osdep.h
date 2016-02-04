/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: osdep.h
 *
 * This file contains highest level definitions, mainly for non-NDIS modules,
 * when they are involved into NDIS driver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef __OS_DEP_H
#define __OS_DEP_H

#pragma warning (push)
#pragma warning (disable:4201)
#pragma warning (disable:28301)
#pragma warning (disable:28252)
#pragma warning (disable:28251)
#include <ndis.h>
#pragma warning (pop)

#pragma warning (push)
#pragma warning (disable:6102)
#include <Ntstrsafe.h>
#pragma warning (pop)

#if NTDDI_VERSION <= NTDDI_VISTA
#define _Requires_lock_held_(lock)
#define _Acquires_shared_lock_(lock)
#define _Acquires_exclusive_lock_(lock)
#endif

#define mb()   KeMemoryBarrier()
#define rmb()  KeMemoryBarrier()
#define wmb()  KeMemoryBarrier()
#define smp_wmb() KeMemoryBarrier()

#ifndef min
#define min(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#endif

#ifndef max
#define max(_a, _b) (((_a) > (_b)) ? (_a) : (_b))
#endif

#ifndef PARANDIS_MAJOR_DRIVER_VERSION
#error PARANDIS_MAJOR_DRIVER_VERSION not defined
#endif

#ifndef PARANDIS_MINOR_DRIVER_VERSION
#error PARANDIS_MINOR_DRIVER_VERSION not defined
#endif

#if (7-PARANDIS_MAJOR_DRIVER_VERSION-7) == 14
#undef PARANDIS_MAJOR_DRIVER_VERSION
#define PARANDIS_MAJOR_DRIVER_VERSION       101
#endif

#if (7-PARANDIS_MINOR_DRIVER_VERSION-7) == 14
#undef PARANDIS_MINOR_DRIVER_VERSION
#define PARANDIS_MINOR_DRIVER_VERSION       58000
#endif

#endif
