/*
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Mikhail Malyshev <mike.malyshev@gmail.com>
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
#include "viogpu_resources.h"

#if !DBG
#include "viogpu_resources.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuResources::VioGpuResources(void)
{
    PAGED_CODE();
    m_pCommands = NULL;
    m_pReleased = NULL;
}

// Makes a host resource of format, Width x Height, backed by an object with its own pages: size bytes, or BAR 0's at
// *pBar when that is not 0. *ppObj receives it; on failure what was made of it is released. The guest having no pages
// for it gives STATUS_INSUFFICIENT_RESOURCES, the host refusing the resource STATUS_NO_MEMORY.
NTSTATUS VioGpuResources::Create(UINT size,
                                 PPHYSICAL_ADDRESS pBar,
                                 UINT format,
                                 UINT Width,
                                 UINT Height,
                                 VioGpuObj **ppObj)
{
    PAGED_CODE();

    *ppObj = NULL;
    VioGpuObj *pObj = new (NonPagedPoolNx) VioGpuObj();
    if (pObj == NULL || !pObj->Init(size, pBar))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no pages for %dx%d (%d bytes)\n", __FUNCTION__, Width, Height, size));
        delete pObj;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UINT id = m_Idr.GetId();
    NTSTATUS status = m_pCommands->ResourceCreate2D(id, format, Width, Height);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s resource %d (%dx%d) not created (0x%x)\n", __FUNCTION__, id, Width, Height, status));
        // Without an answer the host may make the resource still: it goes again with an UNREF. Its pages were never
        // attached.
        if (VioGpuCommands::NoAnswer(status))
        {
            m_pCommands->ResourceUnref(id);
        }
        delete pObj;
        return status;
    }
    pObj->SetId(id);
    status = m_pCommands->ResourceAttachBacking(id, pObj->GetSGList());
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s resource %d: backing not attached (0x%x)\n", __FUNCTION__, id, status));
        // A command still on the ring may attach the pages yet: they go with a DETACH_BACKING. One the host never saw,
        // or refused, left them alone.
        if (VioGpuCommands::NoAnswer(status) && !VioGpuCommands::NotSent(status))
        {
            Release(pObj);
        }
        else
        {
            m_pCommands->ResourceUnref(id);
            delete pObj;
        }
        return status;
    }
    *ppObj = pObj;
    return STATUS_SUCCESS;
}

// Releases a resource the host has: the host lets go of its pages, and the resource goes. The pages are freed once the
// host has answered the DETACH_BACKING, which is fenced, with success or "no such resource"; on any other outcome the
// object is parked until the device reset (Park, FreeReleased). Its ID is not used again (VioGpuIdr).
void VioGpuResources::Release(VioGpuObj *pObj)
{
    PAGED_CODE();

    UINT id = pObj->GetId();
    NTSTATUS status = m_pCommands->ResourceDetachBacking(id);
    NTSTATUS unref = m_pCommands->ResourceUnref(id);
    if (!NT_SUCCESS(unref))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: RESOURCE_UNREF of resource %d not sent (0x%x): the host keeps it until the reset\n",
                  __FUNCTION__,
                  id,
                  unref));
    }
    // The pages are free once the device has answered that it let them go, or that it has no such resource. A detach it
    // refused otherwise, or did not answer, or that was not sent, may leave them attached.
    if (status != STATUS_SUCCESS && status != STATUS_NOT_FOUND)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: DETACH_BACKING of resource %d got 0x%x: its %Iu bytes wait for the device reset\n",
                  __FUNCTION__,
                  id,
                  status,
                  pObj->GetSize()));
        Park(pObj);
        return;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: resource %d released, %Iu bytes freed\n", __FUNCTION__, id, pObj->GetSize()));
    delete pObj;
}

// Frees the objects whose release got no answer. Only once the device has been reset (VioGpuAdapterClose): the host no
// longer uses their pages.
void VioGpuResources::FreeReleased(void)
{
    PAGED_CODE();

    VioGpuObj *pObj = (VioGpuObj *)InterlockedExchangePointer((PVOID volatile *)&m_pReleased, NULL);
    ULONG count = 0;
    SIZE_T bytes = 0;
    while (pObj != NULL)
    {
        VioGpuObj *pNext = pObj->GetNextReleased();
        count++;
        bytes += pObj->GetSize();
        delete pObj;
        pObj = pNext;
    }
    if (count != 0)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: %d object(s), %Iu bytes freed after the reset\n", __FUNCTION__, count, bytes));
    }
}

PAGED_CODE_SEG_END

// Puts an object on the list of objects whose pages wait for the device reset (FreeReleased). Callable at any IRQL.
void VioGpuResources::Park(VioGpuObj *pObj)
{
    VioGpuObj *pHead;
    do
    {
        pHead = m_pReleased;
        pObj->SetNextReleased(pHead);
    } while (InterlockedCompareExchangePointer((PVOID volatile *)&m_pReleased, pObj, pHead) != pHead);
}
