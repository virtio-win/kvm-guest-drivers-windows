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

// ExAllocatePoolWithTag is deprecated in WDK 22621+ but required for
// Server 2016/2019 compatibility (ExAllocatePool2 needs build 19041+).
#pragma warning(disable : 4996)

extern "C"
{
#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
#include <d3dkmdt.h>
#include <ntstrsafe.h>
}

#include "hw.h"

#define STDVGA_TAG 'DGVS'

//
// Current display mode state, tracked by the driver.
//
typedef struct _STDVGA_CURRENT_MODE
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    UINT SrcModeWidth;
    UINT SrcModeHeight;

    struct
    {
        UINT SourceNotVisible : 1;
        UINT FrameBufferIsActive : 1;
        UINT Unused : 30;
    } Flags;
} STDVGA_CURRENT_MODE, *PSTDVGA_CURRENT_MODE;

//
// Per-device context (created by DxgkDdiCreateDevice).
//
typedef struct _STDVGA_DEVICE
{
    struct _STDVGA_DEVICE_CONTEXT *pAdapter;
    HANDLE hDevice;
} STDVGA_DEVICE, *PSTDVGA_DEVICE;

//
// Allocation private driver data (stored with each allocation).
//
typedef struct _STDVGA_ALLOCATION
{
    UINT Width;
    UINT Height;
    UINT Pitch;
    UINT BytesPerPixel;
    BOOLEAN IsPrimary;
    D3DDDIFORMAT Format;
} STDVGA_ALLOCATION, *PSTDVGA_ALLOCATION;

//
// Per-adapter device context.
//
typedef struct _STDVGA_DEVICE_CONTEXT
{
    DEVICE_OBJECT *pPhysicalDevice;
    DXGKRNL_INTERFACE DxgkInterface;
    DXGK_DEVICE_INFO DeviceInfo;
    DXGK_START_INFO StartInfo;

    DEVICE_POWER_STATE AdapterPowerState;
    DEVICE_POWER_STATE MonitorPowerState;

    STDVGA_HW_CONTEXT Hw;
    STDVGA_CURRENT_MODE CurrentMode;

    BOOLEAN DriverStarted;

    UINT LastSubmittedFenceId;
    UINT LastCompletedFenceId;

    //
    // Hot-plug worker thread tracking. Required so StopDevice can
    // synchronously wait for the worker to exit before the driver
    // image is unmapped (otherwise: BugCheck 0xCE).
    //
    PETHREAD HotPlugThread;
    KEVENT HotPlugStopEvent;
} STDVGA_DEVICE_CONTEXT, *PSTDVGA_DEVICE_CONTEXT;
