/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: Array.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Simple dynamic array implementation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"
#include "vioinput.h"

#if defined(EVENT_TRACING)
#include "Array.tmh"
#endif

BOOLEAN DynamicArrayReserve(PDYNAMIC_ARRAY pArray, SIZE_T cbSize)
{
    if (pArray->Ptr == (PVOID)-1)
    {
        // an allocation has failed before, this array is no good
        return FALSE;
    }

    if (pArray->Ptr == NULL)
    {
        // initial allocation
        pArray->Size = 0;
        pArray->MaxSize = cbSize;
        pArray->Ptr = ExAllocatePoolWithTag(
            NonPagedPool,
            pArray->MaxSize,
            VIOINPUT_DRIVER_MEMORY_TAG);
        if (pArray->Ptr == NULL)
        {
            // mark as failed
            pArray->Ptr = (PVOID)-1;
            return FALSE;
        }
    }

    if (cbSize > pArray->MaxSize)
    {
        // resize needed
        PVOID OldPtr = pArray->Ptr;
        while (cbSize > pArray->MaxSize)
        {
            pArray->MaxSize *= 2;
        }
        pArray->Ptr = ExAllocatePoolWithTag(
            NonPagedPool,
            pArray->MaxSize,
            VIOINPUT_DRIVER_MEMORY_TAG);
        if (pArray->Ptr == NULL)
        {
            // mark as failed
            pArray->Ptr = (PVOID)-1;
            return FALSE;
        }

        RtlCopyMemory(pArray->Ptr, OldPtr, pArray->Size);
        ExFreePoolWithTag(OldPtr, VIOINPUT_DRIVER_MEMORY_TAG);
    }
    return TRUE;
}

BOOLEAN DynamicArrayAppend(PDYNAMIC_ARRAY pArray, PVOID pData, SIZE_T cbLength)
{
    if (DynamicArrayReserve(pArray, pArray->Size + cbLength))
    {
        RtlCopyBytes((PUCHAR)pArray->Ptr + pArray->Size, pData, cbLength);
        pArray->Size += cbLength;
        return TRUE;
    }
    return FALSE;
}

VOID DynamicArrayDestroy(PDYNAMIC_ARRAY pArray)
{
    if (pArray->Ptr != NULL && pArray->Ptr != (PVOID)-1)
    {
        ExFreePoolWithTag(pArray->Ptr, VIOINPUT_DRIVER_MEMORY_TAG);
        pArray->Ptr = NULL;
    }
}

PVOID DynamicArrayGet(PDYNAMIC_ARRAY pArray, SIZE_T *pcbSize)
{
    PVOID Ptr = pArray->Ptr;
    if (Ptr == NULL || Ptr == (PVOID)-1)
    {
        if (pcbSize)
        {
            *pcbSize = 0;
        }
        return NULL;
    }
    if (pcbSize)
    {
        *pcbSize = pArray->Size;
    }
    // transferring ownership of the array to the caller
    pArray->Ptr = NULL;
    return Ptr;
}
