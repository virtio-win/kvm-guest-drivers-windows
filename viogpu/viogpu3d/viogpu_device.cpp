#include "viogpu_device.h"
#include "viogpu_adapter.h"
#include "baseobj.h"
#include "virgl_hw.h"

PAGED_CODE_SEG_BEGIN

VioGpuDevice::VioGpuDevice(VioGpuAdapter* pAdapter) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s", __FUNCTION__));

    m_pAdapter = pAdapter;
    m_id = pAdapter->ctxIdr.GetId();
    pAdapter->ctrlQueue.CreateCtx(m_id, 0);
}

VioGpuDevice::~VioGpuDevice() {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s", __FUNCTION__));

    m_pAdapter->ctrlQueue.DestroyCtx(m_id);
    m_pAdapter->ctxIdr.PutId(m_id);
}


NTSTATUS VioGpuDevice::Init(VIOGPU_CTX_INIT_REQ* pOptions) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s", __FUNCTION__));

    UINT context_init = 0;
    
    context_init |= pOptions->CapsetID;

    m_pAdapter->ctrlQueue.DestroyCtx(m_id); // Destroy old viogpu context
    m_pAdapter->ctrlQueue.CreateCtx(m_id, context_init);

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDevice::GenerateBltPresent(DXGKARG_PRESENT* pPresent, VioGpuAllocation *src, VioGpuAllocation *dst) {
    UCHAR* dmaBuf = (UCHAR*)pPresent->pDmaBuffer;
    
    // Calculate rect covering all SubRectx
    RECT coverRect = pPresent->pDstSubRects[0]; 
    for (UINT i = 1; i < pPresent->SubRectCnt; i++) {
        coverRect.top = min(coverRect.top, pPresent->pDstSubRects[i].top);
        coverRect.left = min(coverRect.left, pPresent->pDstSubRects[i].left);
        coverRect.right = min(coverRect.right, pPresent->pDstSubRects[i].right);
        coverRect.bottom = min(coverRect.bottom, pPresent->pDstSubRects[i].bottom);
    }

    INT dx = pPresent->SrcRect.left - pPresent->DstRect.left;
    INT dy = pPresent->SrcRect.top - pPresent->DstRect.top;

    // If source requires coherency (staging or shadow surface) then emit transfer
    if (src->IsCoherent()) {
        VIOGPU_COMMAND_HDR* cmd_hdr = (VIOGPU_COMMAND_HDR*)dmaBuf;
        cmd_hdr->type = VIOGPU_CMD_TRANSFER_TO_HOST;
        cmd_hdr->size = sizeof(VIOGPU_TRANSFER_CMD);
        dmaBuf += sizeof(VIOGPU_COMMAND_HDR);

        VIOGPU_TRANSFER_CMD* cmdBody = (VIOGPU_TRANSFER_CMD*)dmaBuf;
        dmaBuf += sizeof(VIOGPU_TRANSFER_CMD);

        cmdBody->res_id = src->GetId();

        cmdBody->box.x = coverRect.left + dx;
        cmdBody->box.y = coverRect.top + dy;
        cmdBody->box.z = 0;
        cmdBody->box.width = coverRect.right - coverRect.left;
        cmdBody->box.height = coverRect.bottom - coverRect.top;
        cmdBody->box.depth = 1;

        cmdBody->layer_stride = 0;
        cmdBody->stride = 0;
        cmdBody->level = 0;
        cmdBody->offset = 0;
    }

    {
        UINT sizeOfOneRect = 4 * (VIRGL_CMD_RESOURCE_COPY_REGION_SIZE + 1);

        // TODO: Support MultiPassOffset
        UINT rectCnt = min(pPresent->SubRectCnt, (pPresent->DmaSize - 0x100) / sizeOfOneRect);
        
        VIOGPU_COMMAND_HDR* cmd_hdr = (VIOGPU_COMMAND_HDR*)dmaBuf;
        cmd_hdr->type = VIOGPU_CMD_SUBMIT;
        cmd_hdr->size = rectCnt * sizeOfOneRect;
        dmaBuf += sizeof(VIOGPU_COMMAND_HDR);

        for (UINT i = 0; i < rectCnt; i++) {
            UINT* cmdBody = (UINT*)dmaBuf;
            dmaBuf += sizeOfOneRect;

            RECT rect = pPresent->pDstSubRects[i];

            cmdBody[0] = VIRGL_CMD0(VIRGL_CCMD_RESOURCE_COPY_REGION, 0, VIRGL_CMD_RESOURCE_COPY_REGION_SIZE);
            cmdBody[1] = dst->GetId();
            cmdBody[2] = 0;
            cmdBody[3] = rect.left;
            cmdBody[4] = rect.top;
            cmdBody[5] = 0;

            cmdBody[6] = src->GetId();
            cmdBody[7] = 0;
            cmdBody[8] = rect.left + dx;
            cmdBody[9] = rect.top + dy;
            cmdBody[10] = 0;
            cmdBody[11] = rect.right - rect.left;
            cmdBody[12] = rect.bottom - rect.top;
            cmdBody[13] = 1;
        }
    }

    if(dst->IsCoherent()) {
        VIOGPU_COMMAND_HDR* cmd_hdr = (VIOGPU_COMMAND_HDR*)dmaBuf;
        cmd_hdr->type = VIOGPU_CMD_TRANSFER_FROM_HOST;
        cmd_hdr->size = sizeof(VIOGPU_TRANSFER_CMD);
        dmaBuf += sizeof(VIOGPU_COMMAND_HDR);

        VIOGPU_TRANSFER_CMD* cmdBody = (VIOGPU_TRANSFER_CMD*)dmaBuf;
        dmaBuf += sizeof(VIOGPU_TRANSFER_CMD);

        cmdBody->res_id = dst->GetId();

        cmdBody->box.x = coverRect.left;
        cmdBody->box.y = coverRect.top;
        cmdBody->box.z = 0;
        cmdBody->box.width = coverRect.right - coverRect.left;
        cmdBody->box.height = coverRect.bottom - coverRect.top;
        cmdBody->box.depth = 1;

        cmdBody->layer_stride = 0;
        cmdBody->stride = 0;
        cmdBody->level = 0;
        cmdBody->offset = 0;
    }

    pPresent->pDmaBuffer = dmaBuf;

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDevice::Present(_Inout_ DXGKARG_PRESENT* pPresent)
{
    PAGED_CODE();

    if (pPresent->Flags.Flip) return STATUS_SUCCESS;

    DbgPrint(
        TRACE_LEVEL_VERBOSE,
        ("<---> %s Flags=%s %s %s %s %s %s %s %s)\n",
            __FUNCTION__,
            pPresent->Flags.Blt ? "Blt" : "",
            pPresent->Flags.ColorFill ? "ColorFill" : "",
            pPresent->Flags.Flip ? "Flip" : "",
            pPresent->Flags.FlipWithNoWait ? "FlipWithNoWait" : "",
            pPresent->Flags.SrcColorKey ? "SrcColorKey" : "",
            pPresent->Flags.DstColorKey ? "DstColorKey" : "",
            pPresent->Flags.LinearToSrgb ? "LinearToSrgb" : "",
            pPresent->Flags.Rotate ? "Rotate" : ""));

    VioGpuCommand* cmd = new(NonPagedPoolNx) VioGpuCommand(m_pAdapter);
    if (pPresent->pDmaBuffer) {
        VioGpuCommand** privateData = (VioGpuCommand**)pPresent->pDmaBufferPrivateData;
        *privateData = cmd;
    }

    cmd->SetDmaBuf((char*)pPresent->pDmaBuffer);

    DXGK_ALLOCATIONLIST* dxgk_src = &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
    DXGK_ALLOCATIONLIST* dxgk_dst = &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];

    VioGpuAllocation* src = NULL;
    VioGpuAllocation* dst = NULL;;

    if (dxgk_src->hDeviceSpecificAllocation != NULL) {
        src = reinterpret_cast<VioGpuDeviceAllocation*>(dxgk_src->hDeviceSpecificAllocation)->GetAllocation();
        if (pPresent->pDmaBuffer) {
            pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
            pPresent->pPatchLocationListOut->AllocationOffset = 0;
            pPresent->pPatchLocationListOut->DriverId = 1;
            pPresent->pPatchLocationListOut->SlotId = 1;
            pPresent->pPatchLocationListOut->PatchOffset = 0;
            pPresent->pPatchLocationListOut->SplitOffset = 0;

            pPresent->pPatchLocationListOut += 1;
        }
    }

    if (dxgk_dst != NULL) {
        dst = reinterpret_cast<VioGpuDeviceAllocation*>(dxgk_dst->hDeviceSpecificAllocation)->GetAllocation();
        if (pPresent->pDmaBuffer) {
            pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
            pPresent->pPatchLocationListOut->AllocationOffset = 0;
            pPresent->pPatchLocationListOut->DriverId = 2;
            pPresent->pPatchLocationListOut->SlotId = 2;
            pPresent->pPatchLocationListOut->PatchOffset = 0;
            pPresent->pPatchLocationListOut->SplitOffset = 0;

            pPresent->pPatchLocationListOut += 1;
        }
    }

    if (pPresent->Flags.Blt) {
        if (pPresent->pDmaBuffer && dst && src) {
            GenerateBltPresent(pPresent, src, dst);
        }

    } else {
        if (pPresent->pDmaBuffer) {
            VIOGPU_COMMAND_HDR* cmd_hdr = (VIOGPU_COMMAND_HDR*)pPresent->pDmaBuffer;
            cmd_hdr->type = VIOGPU_CMD_NOP;
            cmd_hdr->size = 0;
            pPresent->pDmaBuffer = (char*)pPresent->pDmaBuffer + sizeof(VIOGPU_COMMAND_HDR);
        }
            
        DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s Unsupported PRESENT\n", __FUNCTION__));
    }


    return STATUS_SUCCESS;
}


NTSTATUS VioGpuDevice::Render(DXGKARG_RENDER *pRender) {
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    char* pDmaBufStart = (char*)pRender->pDmaBuffer;

    __try
    {
        pRender->PatchLocationListOutSize = pRender->PatchLocationListInSize;
        for (UINT i = 0; i < pRender->PatchLocationListOutSize; i++) {
            pRender->pPatchLocationListOut->AllocationIndex = pRender->pPatchLocationListIn[i].AllocationIndex;
            pRender->pPatchLocationListOut->AllocationOffset = 0;
            pRender->pPatchLocationListOut->PatchOffset = 0;
            pRender->pPatchLocationListOut->SplitOffset = 0;
            pRender->pPatchLocationListOut->SlotId = i;

            pRender->pPatchLocationListOut++;
        }

        unsigned char* dmaBuf = (unsigned char*)pRender->pDmaBuffer;
        unsigned char* cmdBuf = (unsigned char*)pRender->pCommand;
        unsigned char* endBuf = cmdBuf + pRender->CommandLength;
        while (cmdBuf < endBuf) {
            if (cmdBuf + sizeof(VIOGPU_COMMAND_HDR) > endBuf) {
                return STATUS_INVALID_USER_BUFFER;
            }

            memcpy(dmaBuf, cmdBuf, sizeof(VIOGPU_COMMAND_HDR));
            VIOGPU_COMMAND_HDR* cmdHdr = (VIOGPU_COMMAND_HDR*)dmaBuf;
            cmdBuf += sizeof(VIOGPU_COMMAND_HDR);
            dmaBuf += sizeof(VIOGPU_COMMAND_HDR);

            if (cmdBuf + cmdHdr->size > endBuf) {
                return STATUS_INVALID_USER_BUFFER;
            }

            // Copy command body
            memcpy(dmaBuf, cmdBuf, cmdHdr->size);
            dmaBuf += cmdHdr->size;
            cmdBuf += cmdHdr->size;
        }
        pRender->pDmaBuffer = dmaBuf;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<---> %s Usermode copy exception", __FUNCTION__));
        return STATUS_INVALID_PARAMETER;
    }


    VioGpuCommand* cmd = new(NonPagedPoolNx) VioGpuCommand(m_pAdapter);
    if (pRender->pDmaBuffer) {
        VioGpuCommand** privateData = (VioGpuCommand**)pRender->pDmaBufferPrivateData;
        *privateData = cmd;
    }
    cmd->SetDmaBuf(pDmaBufStart);
    cmd->AttachAllocations(pRender->pAllocationList, pRender->AllocationListSize);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__)); 
    
    return STATUS_SUCCESS;
};

NTSTATUS VioGpuDevice::OpenAllocation(_In_ CONST DXGKARG_OPENALLOCATION* pOpenAllocation)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    for (UINT i = 0; i < pOpenAllocation->NumAllocations; i++)
    {
        DXGK_OPENALLOCATIONINFO* openAllocationInfo = &pOpenAllocation->pOpenAllocation[i];
        VioGpuAllocation* allocation = m_pAdapter->AllocationFromHandle(openAllocationInfo->hAllocation);
        openAllocationInfo->hDeviceSpecificAllocation = new (NonPagedPoolNx)VioGpuDeviceAllocation(this, allocation);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

CtrlQueue *VioGpuDevice::GetCtrlQueue() {
    PAGED_CODE();

    return &m_pAdapter->ctrlQueue;
}


VioGpuDeviceAllocation::VioGpuDeviceAllocation(VioGpuDevice *device, VioGpuAllocation* allocation) {
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d ctx_id=%d\n", __FUNCTION__, allocation->GetId(), device->GetId()));

    m_pAllocation = allocation;
    m_pDevice = device; 
    
    m_pDevice->GetCtrlQueue()->CtxResource(true, m_pDevice->GetId(), m_pAllocation->GetId());
}

VioGpuDeviceAllocation::~VioGpuDeviceAllocation() {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d ctx_id=%d\n", __FUNCTION__, m_pAllocation->GetId(), m_pDevice->GetId()));

    m_pDevice->GetCtrlQueue()->CtxResource(false, m_pDevice->GetId(), m_pAllocation->GetId());
}

VioGpuAllocation* VioGpuDeviceAllocation::GetAllocation() {
    PAGED_CODE();

    return m_pAllocation;
}

PAGED_CODE_SEG_END