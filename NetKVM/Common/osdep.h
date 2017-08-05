/**********************************************************************
 * Copyright (c) 2008-2017 Red Hat, Inc.
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
#pragma once

#include <ndis.h>
#include <Ntstrsafe.h>

#if NTDDI_VERSION <= NTDDI_VISTASP1
#define _Requires_lock_held_(lock)
#define _Acquires_shared_lock_(lock)
#define _Acquires_exclusive_lock_(lock)
#endif

#if ((OSVERSION_MASK & NTDDI_VERSION) == NTDDI_VISTA)
#define NDIS_PROTOCOL_ID_IP6            0x03

typedef struct _NETWORK_ADDRESS_IP6 {
    USHORT      sin6_port;
    ULONG       sin6_flowinfo;
    USHORT      sin6_addr[8];
    ULONG       sin6_scope_id;
} NETWORK_ADDRESS_IP6, *PNETWORK_ADDRESS_IP6;
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
