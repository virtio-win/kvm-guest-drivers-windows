/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#include "helper.h"
#include "baseobj.h"
#if !DBG
#include "viogpu_idr.tmh"
#endif

VioGpuIdr::VioGpuIdr()
{
    m_uStartIndex = 0;
    m_IdBitMap.SizeOfBitMap = 0;
    m_IdBitMap.Buffer = NULL;
    ExInitializeFastMutex(&m_IdBitMapMutex);
}

VioGpuIdr::~VioGpuIdr()
{
    Close();
}

BOOLEAN VioGpuIdr::Init(
    _In_ ULONG start)
{
    PUCHAR buf = NULL;
    BOOLEAN ret = FALSE;
    m_uStartIndex = start;

    VIOGPU_ASSERT(m_IdBitMap.Buffer == NULL);
    Lock();
    buf = new (NonPagedPoolNx) UCHAR[PAGE_SIZE];
    RtlZeroMemory(buf, (PAGE_SIZE * sizeof(UCHAR)));
    if (buf) {
        RtlInitializeBitMap(&m_IdBitMap,
            (PULONG)buf,
            CHAR_BIT * PAGE_SIZE);
        RtlClearAllBits(&m_IdBitMap);
        RtlSetBits(&m_IdBitMap, 0, m_uStartIndex);
        ret = TRUE;
    }
    Unlock();
    return ret;
}

#pragma warning( disable: 28167 )
//_IRQL_raises_(APC_LEVEL)
//_IRQL_saves_global_(OldIrql, m_IdBitMapMutex)
VOID VioGpuIdr::Lock(VOID)
{
    ExAcquireFastMutex(&m_IdBitMapMutex);
}

#pragma warning( disable: 28167 )
//_IRQL_requires_(APC_LEVEL)
//_IRQL_restores_global_(OldIrql, m_IdBitMapMutex)
VOID VioGpuIdr::Unlock(VOID)
{
    ExReleaseFastMutex(&m_IdBitMapMutex);
}

ULONG VioGpuIdr::GetId(VOID)
{
    ULONG id = 0;
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        id = RtlFindClearBitsAndSet(&m_IdBitMap, 1, 0);
    }
    Unlock();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("[%s] id = %d\n", __FUNCTION__, id));
    ASSERT(id < USHORT_MAX);
    return id;
}

VOID VioGpuIdr::PutId(_In_ ULONG id)
{
    ASSERT(id >= m_uStartIndex);
    ASSERT(id <= (CHAR_BIT * PAGE_SIZE));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("[%s] bit %d\n", __FUNCTION__, id));
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        if (!RtlAreBitsSet(&m_IdBitMap, id, 1))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("[%s] bit %d is not set\n", __FUNCTION__, id - m_uStartIndex));
        }
        RtlClearBits(&m_IdBitMap, id, 1);
    }
    Unlock();
}

VOID VioGpuIdr::Close(VOID)
{
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        delete[] m_IdBitMap.Buffer;
        m_IdBitMap.Buffer = NULL;
    }
    m_uStartIndex = 0;
    Unlock();
}
