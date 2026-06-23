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

#pragma once

#pragma warning(disable : 4996)

extern "C"
{
#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
}

//
// PCI identification
//
#define STDVGA_PCI_VENDOR_ID             0x1234
#define STDVGA_PCI_DEVICE_ID             0x1111

//
// Bochs VBE dispi register indices
//
#define VBE_DISPI_INDEX_ID               0
#define VBE_DISPI_INDEX_XRES             1
#define VBE_DISPI_INDEX_YRES             2
#define VBE_DISPI_INDEX_BPP              3
#define VBE_DISPI_INDEX_ENABLE           4
#define VBE_DISPI_INDEX_BANK             5
#define VBE_DISPI_INDEX_VIRT_WIDTH       6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      7
#define VBE_DISPI_INDEX_X_OFFSET         8
#define VBE_DISPI_INDEX_Y_OFFSET         9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 10

//
// VBE dispi ID versions
//
#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

//
// VBE dispi enable flags
//
#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

//
// I/O port addresses (fallback when MMIO BAR2 not available)
//
#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01D0

//
// MMIO BAR2 offsets
//
#define VBE_DISPI_MMIO_OFFSET            0x0500
#define VGA_MMIO_OFFSET                  0x0400

//
// PCI BAR indices
//
#define STDVGA_BAR_FRAMEBUFFER           0
#define STDVGA_BAR_MMIO                  2

//
// Default BPP
//
#define STDVGA_DEFAULT_BPP               32
#define STDVGA_BYTES_PER_PIXEL           4

//
// Mode definition
//
typedef struct _STDVGA_MODE
{
    USHORT Width;
    USHORT Height;
} STDVGA_MODE, *PSTDVGA_MODE;

//
// Hardware state
//
typedef struct _STDVGA_HW_CONTEXT
{
    PHYSICAL_ADDRESS FrameBufferPA;
    SIZE_T FrameBufferLength;
    PVOID pFrameBuffer;

    PHYSICAL_ADDRESS MmioPA;
    SIZE_T MmioLength;
    PVOID pMmio;
    BOOLEAN UseMmio;

    SIZE_T VramSize;

    PSTDVGA_MODE pModeList;
    ULONG ModeCount;

    USHORT CurrentWidth;
    USHORT CurrentHeight;
    USHORT CurrentStridePixels; // VBE-aligned stride (>=Width)
} STDVGA_HW_CONTEXT, *PSTDVGA_HW_CONTEXT;

NTSTATUS
StdVgaHwInit(_Inout_ PSTDVGA_HW_CONTEXT HwContext, _In_ PCM_RESOURCE_LIST TranslatedResources);

VOID StdVgaHwClose(_Inout_ PSTDVGA_HW_CONTEXT HwContext);

NTSTATUS
StdVgaHwSetMode(_Inout_ PSTDVGA_HW_CONTEXT HwContext, _In_ USHORT Width, _In_ USHORT Height);

VOID StdVgaHwBlackOut(_In_ PSTDVGA_HW_CONTEXT HwContext);

SIZE_T
StdVgaHwGetVramSize(_In_ PSTDVGA_HW_CONTEXT HwContext);

NTSTATUS
StdVgaHwBuildModeList(_Inout_ PSTDVGA_HW_CONTEXT HwContext);

PVOID
StdVgaHwGetFrameBuffer(_In_ PSTDVGA_HW_CONTEXT HwContext);

VOID StdVgaHwGetCurrentMode(_In_ PSTDVGA_HW_CONTEXT HwContext, _Out_ PUSHORT pWidth, _Out_ PUSHORT pHeight);
