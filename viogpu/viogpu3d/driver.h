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

#pragma once
#include "helper.h"

extern "C"
DRIVER_INITIALIZE DriverEntry;

VOID
VioGpu3DUnload(VOID);

NTSTATUS
VioGpu3DAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext);

NTSTATUS
VioGpu3DRemoveDevice(
    _In_  VOID* pDeviceContext);

NTSTATUS
VioGpu3DStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren);

NTSTATUS
VioGpu3DStopDevice(
    _In_  VOID* pDeviceContext);

VOID
VioGpu3DResetDevice(
    _In_  VOID* pDeviceContext);


NTSTATUS
VioGpu3DDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);

NTSTATUS
VioGpu3DSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType);

NTSTATUS
VioGpu3DQueryChildRelations(
    _In_                             VOID*                  pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_                             ULONG                  ChildRelationsSize);

NTSTATUS
VioGpu3DQueryChildStatus(
    _In_    VOID*              pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly);

NTSTATUS
VioGpu3DQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

BOOLEAN
VioGpu3DInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber);

VOID
VioGpu3DDpcRoutine(
    _In_  VOID* pDeviceContext);

NTSTATUS
APIENTRY
VioGpu3DQueryAdapterInfo(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo);

NTSTATUS
APIENTRY
VioGpu3DDdiGetNodeMetadata(
    _In_ CONST HANDLE               hAdapter,
    UINT                            NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA* pGetNodeMetadata);

NTSTATUS
APIENTRY
VioGpu3DSetPointerPosition(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition);

NTSTATUS
APIENTRY
VioGpu3DSetPointerShape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape);

NTSTATUS
APIENTRY
VioGpu3DEscape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_ESCAPE*                pEscape);


NTSTATUS
APIENTRY
VioGpu3DCreateAllocation(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION*         pCreateAllocation);

NTSTATUS
APIENTRY
VioGpu3DOpenAllocation(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_OPENALLOCATION*        pOpenAllocation);

NTSTATUS
APIENTRY
VioGpu3DCloseAllocation(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_CLOSEALLOCATION*       pCloseAllocation);

NTSTATUS
APIENTRY
VioGpu3DDescribeAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation
);

NTSTATUS
APIENTRY
VioGpu3DDestroyAllocation(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION*     pDestroyAllocation);


NTSTATUS
APIENTRY
VioGpu3DGetStandardAllocationDriverData(
    _In_ CONST HANDLE                                hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pStandardAllocation
);


NTSTATUS
APIENTRY
VioGpu3DBuildPagingBuffer(
    _In_ CONST HANDLE                 hAdapter,
    _In_ DXGKARG_BUILDPAGINGBUFFER* pCreateAllocation
);

NTSTATUS
APIENTRY
VioGpu3DPatch(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_PATCH*         pPatch);

NTSTATUS
APIENTRY
VioGpu3DSubmitCommand(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND* pSubmitCommand);

NTSTATUS
APIENTRY
VioGpu3DCreateDevice(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE* pCreateDevice
);


NTSTATUS
APIENTRY
VioGpu3DDestroyDevice(
    _In_  VOID* pDeviceContext);


NTSTATUS 
APIENTRY
VioGpu3DDdiCreateContext(
    _In_    CONST HANDLE           hDevice,
    _Inout_ DXGKARG_CREATECONTEXT* pCreateContext);

NTSTATUS 
APIENTRY
VioGpu3DDdiDestroyContext(
    _In_    CONST HANDLE             hContext);

NTSTATUS
APIENTRY
VioGpu3DPresent(
    _In_ CONST HANDLE              hDevice,
    _Inout_   DXGKARG_PRESENT*     pPresent);

NTSTATUS
APIENTRY
VioGpu3DRender(
    _In_ CONST HANDLE              hDevice,
    _Inout_   DXGKARG_RENDER*      pRender);

NTSTATUS
APIENTRY
VioGpu3DIsSupportedVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn);

NTSTATUS
APIENTRY
VioGpu3DRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                   hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn);

NTSTATUS
APIENTRY
VioGpu3DRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology);

NTSTATUS
APIENTRY
VioGpu3DRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes);

NTSTATUS
APIENTRY
VioGpu3DEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality);

NTSTATUS
APIENTRY
VioGpu3DSetVidPnSourceVisibility(
    _In_ CONST HANDLE                             hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility);

NTSTATUS VioGpu3DSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress
);

NTSTATUS
APIENTRY
VioGpu3DCommitVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn);

NTSTATUS
APIENTRY
VioGpu3DUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                       hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath);

NTSTATUS
APIENTRY
VioGpu3DQueryVidPnHWCapability(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps);

NTSTATUS
APIENTRY
VioGpu3DDdiControlInterrupt(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE    InterruptType,
    _In_ BOOLEAN                      EnableInterrupt);

NTSTATUS
APIENTRY
VioGpu3DDdiGetScanLine(
    _In_    CONST HANDLE         hAdapter,
    _Inout_ DXGKARG_GETSCANLINE* pGetScanLine
);

NTSTATUS
APIENTRY
VioGpu3DStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo);

NTSTATUS
APIENTRY
VioGpu3DSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat);

VOID
APIENTRY
VioGpu3DSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY);

NTSTATUS
APIENTRY
VioGpu3DDdiPreemptCommand(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND* pPreemptCommand);

NTSTATUS
APIENTRY
VioGpu3DDdiRestartFromTimeout(
    _In_ CONST HANDLE     hAdapter);


NTSTATUS
APIENTRY
VioGpu3DDdiCancelCommand(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_CANCELCOMMAND* pCancelCommand);

NTSTATUS
APIENTRY
VioGpu3DDdiQueryCurrentFence(
    _In_    CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE* pCurrentFence);

NTSTATUS
APIENTRY
VioGpu3DDdiResetEngine(
    _In_    CONST HANDLE          hAdapter,
    _Inout_ DXGKARG_RESETENGINE* pResetEngine);

NTSTATUS
APIENTRY
VioGpu3DDdiQueryEngineStatus(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYENGINESTATUS* pQueryEngineStatus);

NTSTATUS
APIENTRY
VioGpu3DDdiCollectDbgInfo(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COLLECTDBGINFO* pCollectDbgInfo);

NTSTATUS
APIENTRY
VioGpu3DDdiResetFromTimeout(_In_ CONST HANDLE hAdapter);
