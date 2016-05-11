/**********************************************************************
* Copyright (c) 2008-2016 Red Hat, Inc.
*
* File: ParaNdis-VirtIO.c
*
* This file contains NDIS driver VirtIO callbacks
*
* This work is licensed under the terms of the GNU GPL, version 2.  See
* the COPYING file in the top-level directory.
*
**********************************************************************/
#include "ndis56common.h"

/////////////////////////////////////////////////////////////////////////////////////
//
// ReadVirtIODeviceRegister\WriteVirtIODeviceRegister
// NDIS specific implementation of the IO and memory space read\write
//
// The lower 64k of memory is never mapped so we can use the same routines
// for both port I/O and memory access and use the address alone to decide
// which space to use.
/////////////////////////////////////////////////////////////////////////////////////

#define PORT_MASK 0xFFFF

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    ULONG ulValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUlong(ulRegister, &ulValue);
    } else {
        NdisRawReadPortUlong(ulRegister, &ulValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue));
    return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUlong((PULONG)ulRegister, ulValue);
    } else {
        NdisRawWritePortUlong(ulRegister, ulValue);
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    u8 bValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUchar(ulRegister, &bValue);
    } else {
        NdisRawReadPortUchar(ulRegister, &bValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue));
    return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUchar((PUCHAR)ulRegister, bValue);
    } else {
        NdisRawWritePortUchar(ulRegister, bValue);
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    u16 wValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUshort(ulRegister, &wValue);
    } else {
        NdisRawReadPortUshort(ulRegister, &wValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
#if 1
    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUshort((PUSHORT)ulRegister, wValue);
    } else {
        NdisRawWritePortUshort(ulRegister, wValue);
    }
#else
    // test only to cause long TX waiting queue of NDIS packets
    // to recognize it and request for reset via Hang handler
    static int nCounterToFail = 0;
    static const int StartFail = 200, StopFail = 600;
    BOOLEAN bFail = FALSE;
    DPrintf(6, ("%s> R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    if ((ulRegister & 0x1F) == 0x10)
    {
        nCounterToFail++;
        bFail = nCounterToFail >= StartFail && nCounterToFail < StopFail;
    }
    if (!bFail) NdisRawWritePortUshort(ulRegister, wValue);
    else
    {
        DPrintf(0, ("%s> FAILING R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    }
#endif
}
