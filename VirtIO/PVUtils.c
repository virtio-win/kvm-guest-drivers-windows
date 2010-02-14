/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: PVUtils.c
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
#include "PVUtils.h"

/////////////////////////////////////////////////////////////////////////////////////
//
// AllocatePhysical - Utility function for allocating physical memory
//
/////////////////////////////////////////////////////////////////////////////////////
PVOID AllocatePhysical(ULONG uSize)
{
	PHYSICAL_ADDRESS HighestAcceptable;

#ifdef _WIN64
	HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
#else
	HighestAcceptable.QuadPart = (ULONG)-1;
#endif

	return MmAllocateContiguousMemory(uSize,HighestAcceptable);
}

void FreePhysical(PVOID addr)
{
	MmFreeContiguousMemory(addr);
}


PHYSICAL_ADDRESS GetPhysicalAddress(PVOID addr)
{
	return MmGetPhysicalAddress(addr);
}
