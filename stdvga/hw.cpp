/*
 * Copyright (c) 2026 Alibaba Cloud Computing Ltd.
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

#include "hw.h"

#pragma code_seg("PAGE")

// clang-format off
static const STDVGA_MODE s_DefaultModes[] = {
    {800, 600},
    {1024, 768},
    {1152, 864},
    {1280, 720},
    {1280, 768},
    {1280, 800},
    {1280, 960},
    {1280, 1024},
    {1400, 1050},
    {1440, 900},
    {1600, 1200},
    {1680, 1050},
    {1920, 1080},
    {1920, 1200},
    {2560, 1600},
};
// clang-format on

static const ULONG s_DefaultModeCount = ARRAYSIZE(s_DefaultModes);

//
// Write a 16-bit value to a Bochs VBE dispi register.
//
static VOID BochsWrite(_In_ PSTDVGA_HW_CONTEXT HwContext, _In_ USHORT Index, _In_ USHORT Value)
{
    if (HwContext->UseMmio && HwContext->pMmio != NULL)
    {
        PUSHORT pReg = (PUSHORT)((PUCHAR)HwContext->pMmio + VBE_DISPI_MMIO_OFFSET + (Index << 1));
        WRITE_REGISTER_USHORT(pReg, Value);
    }
    else
    {
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_INDEX, Index);
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_DATA, Value);
    }
}

//
// Read a 16-bit value from a Bochs VBE dispi register.
//
static USHORT BochsRead(_In_ PSTDVGA_HW_CONTEXT HwContext, _In_ USHORT Index)
{
    if (HwContext->UseMmio && HwContext->pMmio != NULL)
    {
        PUSHORT pReg = (PUSHORT)((PUCHAR)HwContext->pMmio + VBE_DISPI_MMIO_OFFSET + (Index << 1));
        return READ_REGISTER_USHORT(pReg);
    }
    else
    {
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_INDEX, Index);
        return READ_PORT_USHORT((PUSHORT)(ULONG_PTR)VBE_DISPI_IOPORT_DATA);
    }
}

_Use_decl_annotations_ NTSTATUS StdVgaHwInit(PSTDVGA_HW_CONTEXT HwContext, PCM_RESOURCE_LIST TranslatedResources)
{
    PAGED_CODE();

    RtlZeroMemory(HwContext, sizeof(*HwContext));

    if (TranslatedResources == NULL || TranslatedResources->Count == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    PCM_FULL_RESOURCE_DESCRIPTOR pFullDesc = &TranslatedResources->List[0];
    ULONG resourceCount = pFullDesc->PartialResourceList.Count;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pPartialDesc = pFullDesc->PartialResourceList.PartialDescriptors;

    for (ULONG i = 0; i < resourceCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pDesc = &pPartialDesc[i];

        switch (pDesc->Type)
        {
            case CmResourceTypeMemory:
            case CmResourceTypeMemoryLarge:
                {
                    PHYSICAL_ADDRESS start = pDesc->u.Memory.Start;
                    SIZE_T length = pDesc->u.Memory.Length;

                    if (length >= 0x100000)
                    {
                        // Large region (>= 1MB) is the framebuffer BAR.
                        HwContext->FrameBufferPA = start;
                        HwContext->FrameBufferLength = length;
                    }
                    else if (length > 0 && length < 0x100000 && start.QuadPart > 0x100000)
                    {
                        // Small region above 1MB is the MMIO BAR.
                        HwContext->MmioPA = start;
                        HwContext->MmioLength = length;
                        HwContext->UseMmio = TRUE;
                    }
                    // Skip legacy VGA aperture (0xA0000-0xBFFFF).
                    break;
                }

            default:
                break;
        }
    }

    if (HwContext->FrameBufferPA.QuadPart == 0 || HwContext->FrameBufferLength == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    //
    // Map MMIO BAR if available.
    //
    if (HwContext->UseMmio && HwContext->MmioLength > 0)
    {
        HwContext->pMmio = MmMapIoSpaceEx(HwContext->MmioPA, HwContext->MmioLength, PAGE_READWRITE | PAGE_NOCACHE);

        if (HwContext->pMmio == NULL)
        {
            HwContext->UseMmio = FALSE;
        }
    }

    //
    // Validate the Bochs VBE interface.
    //
    USHORT bochsId = BochsRead(HwContext, VBE_DISPI_INDEX_ID);
    if ((bochsId & 0xFFF0) != VBE_DISPI_ID0)
    {
        if (HwContext->pMmio != NULL)
        {
            MmUnmapIoSpace(HwContext->pMmio, HwContext->MmioLength);
            HwContext->pMmio = NULL;
        }
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    //
    // Read VRAM size.
    //
    USHORT vram64k = BochsRead(HwContext, VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
    HwContext->VramSize = (SIZE_T)vram64k * 64 * 1024;

    if (HwContext->VramSize == 0)
    {
        HwContext->VramSize = HwContext->FrameBufferLength;
    }

    //
    // Map the framebuffer.
    //
    SIZE_T mapLen = min(HwContext->VramSize, HwContext->FrameBufferLength);
    HwContext->pFrameBuffer = MmMapIoSpaceEx(HwContext->FrameBufferPA, mapLen, PAGE_READWRITE | PAGE_WRITECOMBINE);

    if (HwContext->pFrameBuffer == NULL)
    {
        if (HwContext->pMmio != NULL)
        {
            MmUnmapIoSpace(HwContext->pMmio, HwContext->MmioLength);
            HwContext->pMmio = NULL;
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Build mode list.
    //
    NTSTATUS status = StdVgaHwBuildModeList(HwContext);
    if (!NT_SUCCESS(status))
    {
        MmUnmapIoSpace(HwContext->pFrameBuffer, mapLen);
        HwContext->pFrameBuffer = NULL;
        if (HwContext->pMmio != NULL)
        {
            MmUnmapIoSpace(HwContext->pMmio, HwContext->MmioLength);
            HwContext->pMmio = NULL;
        }
        return status;
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID StdVgaHwClose(PSTDVGA_HW_CONTEXT HwContext)
{
    PAGED_CODE();

    if (HwContext->pModeList != NULL)
    {
        ExFreePoolWithTag(HwContext->pModeList, 'DGVS');
        HwContext->pModeList = NULL;
    }

    if (HwContext->pFrameBuffer != NULL)
    {
        SIZE_T mapLen = min(HwContext->VramSize, HwContext->FrameBufferLength);
        MmUnmapIoSpace(HwContext->pFrameBuffer, mapLen);
        HwContext->pFrameBuffer = NULL;
    }

    if (HwContext->pMmio != NULL)
    {
        MmUnmapIoSpace(HwContext->pMmio, HwContext->MmioLength);
        HwContext->pMmio = NULL;
    }

    HwContext->ModeCount = 0;
}

_Use_decl_annotations_ NTSTATUS StdVgaHwSetMode(PSTDVGA_HW_CONTEXT HwContext, USHORT Width, USHORT Height)
{
    PAGED_CODE();

    // QEMU std-vga (Bochs VBE) requires XRES be a multiple of 8, otherwise
    // the write is silently ignored and XRES stays at the previous value.
    // Round both XRES and VIRT_WIDTH to 8.
    USHORT alignedW = (USHORT)((Width + 7) & ~7);

    SIZE_T requiredVram = (SIZE_T)alignedW * Height * STDVGA_BYTES_PER_PIXEL;
    if (requiredVram > HwContext->VramSize)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    BochsWrite(HwContext, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    BochsWrite(HwContext, VBE_DISPI_INDEX_XRES, alignedW);
    BochsWrite(HwContext, VBE_DISPI_INDEX_YRES, Height);
    BochsWrite(HwContext, VBE_DISPI_INDEX_BPP, STDVGA_DEFAULT_BPP);
    BochsWrite(HwContext, VBE_DISPI_INDEX_VIRT_WIDTH, alignedW);
    BochsWrite(HwContext, VBE_DISPI_INDEX_VIRT_HEIGHT, Height);
    BochsWrite(HwContext, VBE_DISPI_INDEX_X_OFFSET, 0);
    BochsWrite(HwContext, VBE_DISPI_INDEX_Y_OFFSET, 0);
    BochsWrite(HwContext, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    // Driver tracks aligned values so framebuffer writes match hardware stride.
    HwContext->CurrentWidth = alignedW;
    HwContext->CurrentHeight = Height;
    HwContext->CurrentStridePixels = alignedW;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID StdVgaHwBlackOut(PSTDVGA_HW_CONTEXT HwContext)
{
    PAGED_CODE();

    if (HwContext->pFrameBuffer == NULL)
    {
        return;
    }

    SIZE_T size = (SIZE_T)HwContext->CurrentWidth * HwContext->CurrentHeight * STDVGA_BYTES_PER_PIXEL;

    RtlZeroMemory(HwContext->pFrameBuffer, size);
}

_Use_decl_annotations_ SIZE_T StdVgaHwGetVramSize(PSTDVGA_HW_CONTEXT HwContext)
{
    return HwContext->VramSize;
}

_Use_decl_annotations_ NTSTATUS StdVgaHwBuildModeList(PSTDVGA_HW_CONTEXT HwContext)
{
    PAGED_CODE();

    if (HwContext->pModeList != NULL)
    {
        ExFreePoolWithTag(HwContext->pModeList, 'DGVS');
        HwContext->pModeList = NULL;
        HwContext->ModeCount = 0;
    }

    //
    // First pass: count modes that fit in VRAM.
    //
    ULONG count = 0;
    for (ULONG i = 0; i < s_DefaultModeCount; i++)
    {
        SIZE_T required = (SIZE_T)s_DefaultModes[i].Width * s_DefaultModes[i].Height * STDVGA_BYTES_PER_PIXEL;
        if (required <= HwContext->VramSize)
        {
            count++;
        }
    }

    if (count == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    HwContext->pModeList = (PSTDVGA_MODE)ExAllocatePoolWithTag(PagedPool, count * sizeof(STDVGA_MODE), 'DGVS');

    if (HwContext->pModeList == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(HwContext->pModeList, count * sizeof(STDVGA_MODE));

    //
    // Second pass: populate.
    //
    ULONG idx = 0;
    for (ULONG i = 0; i < s_DefaultModeCount; i++)
    {
        SIZE_T required = (SIZE_T)s_DefaultModes[i].Width * s_DefaultModes[i].Height * STDVGA_BYTES_PER_PIXEL;
        if (required <= HwContext->VramSize)
        {
            HwContext->pModeList[idx] = s_DefaultModes[i];
            idx++;
        }
    }

    HwContext->ModeCount = count;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ PVOID StdVgaHwGetFrameBuffer(PSTDVGA_HW_CONTEXT HwContext)
{
    return HwContext->pFrameBuffer;
}

_Use_decl_annotations_ VOID StdVgaHwGetCurrentMode(PSTDVGA_HW_CONTEXT HwContext, PUSHORT pWidth, PUSHORT pHeight)
{
    PAGED_CODE();

    *pWidth = BochsRead(HwContext, VBE_DISPI_INDEX_XRES);
    *pHeight = BochsRead(HwContext, VBE_DISPI_INDEX_YRES);

    if (*pWidth == 0 || *pHeight == 0)
    {
        *pWidth = 1920;
        *pHeight = 1080;
    }
}
