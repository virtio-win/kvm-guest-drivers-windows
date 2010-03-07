/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialMemUtils.c
 *
 * Author:
 * Yan Vugenfirer <yvugenfi@redhat.com>
 *
 * This file contains memory allocation related functions.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VIOSerialMemUtils.h"

/////////////////////////////////////////////////////////////////////////////////////
//
// AllocatePhysical - Utility function for allocating physical memory
//
/////////////////////////////////////////////////////////////////////////////////////
PVOID VIOSerialAllocatePhysical(ULONG uSize)
{
	PHYSICAL_ADDRESS HighestAcceptable;

#ifdef _WIN64
	HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
#else
	HighestAcceptable.QuadPart = (ULONG)-1;
#endif

	return MmAllocateContiguousMemory(uSize,HighestAcceptable);
}

void VIOSerialFreePhysical(PVOID addr)
{
	MmFreeContiguousMemory(addr);
}


PHYSICAL_ADDRESS VIOSerialGetPhysicalAddress(PVOID addr)
{
	return MmGetPhysicalAddress(addr);
}

