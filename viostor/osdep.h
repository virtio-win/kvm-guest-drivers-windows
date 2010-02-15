/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: osdep.h
 *
 * This file contains highest level definitions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef __OS_DEP_H
#define __OS_DEP_H

#include <ntddk.h>

#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned long
#define u64 ULONGLONG

#ifndef bool
#define bool int
#endif

#define mb()   KeMemoryBarrier()
#define rmb()  KeMemoryBarrier()
#define wmb()  KeMemoryBarrier()
#define smp_wmb() KeMemoryBarrier()

#endif
