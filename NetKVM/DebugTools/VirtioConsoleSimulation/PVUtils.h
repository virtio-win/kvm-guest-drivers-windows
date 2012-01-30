/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: PVUtils.c
 *
 * Author:
 * Yan Vugenfirer <yvugenfi@redhat.com>
 *
 * This file contains definitions for memory allocation related functions.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef PV_UTILS_H
#define PV_UTILS_H

PVOID AllocatePhysical(ULONG uSize);
void FreePhysical(PVOID addr);
PHYSICAL_ADDRESS GetPhysicalAddress(PVOID  BaseAddress);

#endif
