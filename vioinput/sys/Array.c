/*
 * Simple dynamic array implementation
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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
        pArray->Ptr = ExAllocatePoolUninitialized(
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
        pArray->Ptr = ExAllocatePoolUninitialized(
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

BOOLEAN DynamicArrayIsEmpty(PDYNAMIC_ARRAY pArray)
{
    return (pArray->Ptr == NULL || pArray->Size == 0);
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
