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

#include "stdvga.h"

//
// DXGK DDI callback implementations.
// These are called by the wrapper functions in driver.cpp.
//

NTSTATUS
StdVgaStartDevice(_Inout_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                  _In_ DXGK_START_INFO *pStartInfo,
                  _In_ DXGKRNL_INTERFACE *pDxgkInterface,
                  _Out_ PULONG pNumberOfVideoPresentSources,
                  _Out_ PULONG pNumberOfChildren);

NTSTATUS
StdVgaStopDevice(_Inout_ PSTDVGA_DEVICE_CONTEXT DevCtx);

VOID StdVgaResetDevice(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx);

NTSTATUS
StdVgaDispatchIoRequest(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                        _In_ ULONG VidPnSourceId,
                        _In_ PVIDEO_REQUEST_PACKET pVideoRequestPacket);

NTSTATUS
StdVgaSetPowerState(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                    _In_ ULONG DeviceUid,
                    _In_ DEVICE_POWER_STATE DevicePowerState,
                    _In_ POWER_ACTION ActionType);

NTSTATUS
StdVgaQueryChildRelations(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                          _Inout_ PDXGK_CHILD_DESCRIPTOR pChildRelations,
                          _In_ ULONG ChildRelationsSize);

NTSTATUS
StdVgaQueryChildStatus(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                       _Inout_ PDXGK_CHILD_STATUS pChildStatus,
                       _In_ BOOLEAN NonDestructiveOnly);

NTSTATUS
StdVgaQueryDeviceDescriptor(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                            _In_ ULONG ChildUid,
                            _Inout_ PDXGK_DEVICE_DESCRIPTOR pDeviceDescriptor);

NTSTATUS
StdVgaQueryAdapterInfo(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo);

NTSTATUS
StdVgaSetPointerPosition(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                         _In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition);

NTSTATUS
StdVgaSetPointerShape(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape);

NTSTATUS
StdVgaEscape(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_ESCAPE *pEscape);

NTSTATUS
StdVgaPresentDisplayOnly(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                         _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly);

NTSTATUS
StdVgaIsSupportedVidPn(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn);

NTSTATUS
StdVgaRecommendFunctionalVidPn(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                               _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn);

NTSTATUS
StdVgaRecommendMonitorModes(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                            _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes);

NTSTATUS
StdVgaEnumVidPnCofuncModality(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                              _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality);

NTSTATUS
StdVgaSetVidPnSourceVisibility(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                               _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility);

NTSTATUS
StdVgaCommitVidPn(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_COMMITVIDPN *pCommitVidPn);

NTSTATUS
StdVgaUpdateActiveVidPnPresentPath(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                                   _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath);

NTSTATUS
StdVgaQueryVidPnHWCapability(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps);

VOID StdVgaDrainHotPlugWorker(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx);

NTSTATUS
StdVgaStopDeviceAndReleasePostDisplayOwnership(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                                               _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                               _Out_ DXGK_DISPLAY_INFORMATION *pDisplayInfo);

NTSTATUS
StdVgaSystemDisplayEnable(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                          _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                          _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                          _Out_ PUINT pWidth,
                          _Out_ PUINT pHeight,
                          _Out_ D3DDDIFORMAT *pColorFormat);

VOID StdVgaSystemDisplayWrite(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                              _In_ PVOID pSource,
                              _In_ UINT SourceWidth,
                              _In_ UINT SourceHeight,
                              _In_ UINT SourceStride,
                              _In_ UINT PositionX,
                              _In_ UINT PositionY);

BOOLEAN
StdVgaInterruptRoutine(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ ULONG MessageNumber);

VOID StdVgaDpcRoutine(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx);

//
// Full WDDM miniport DDIs (memory management, scheduling, rendering).
//

NTSTATUS
StdVgaCreateDevice(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_CREATEDEVICE *pCreateDevice);

NTSTATUS
StdVgaDestroyDevice(_In_ PVOID pDeviceContext);

NTSTATUS
StdVgaCreateAllocation(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_CREATEALLOCATION *pCreateAllocation);

NTSTATUS
StdVgaDestroyAllocation(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation);

NTSTATUS
StdVgaDescribeAllocation(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_DESCRIBEALLOCATION *pDescribeAllocation);

NTSTATUS
StdVgaGetStandardAllocationDriverData(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                                      _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *pStdAllocData);

NTSTATUS
StdVgaOpenAllocation(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_OPENALLOCATION *pOpenAllocation);

NTSTATUS
StdVgaCloseAllocation(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation);

NTSTATUS
StdVgaBuildPagingBuffer(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer);

NTSTATUS
StdVgaSubmitCommand(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand);

NTSTATUS
StdVgaPatch(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_PATCH *pPatch);

NTSTATUS
StdVgaPreemptCommand(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand);

NTSTATUS
StdVgaQueryCurrentFence(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_QUERYCURRENTFENCE *pQueryCurrentFence);

NTSTATUS
StdVgaPresent(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_PRESENT *pPresent);

NTSTATUS
StdVgaRender(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _Inout_ DXGKARG_RENDER *pRender);

NTSTATUS
StdVgaSetVidPnSourceAddress(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                            _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress);

NTSTATUS
StdVgaResetFromTimeout(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx);

NTSTATUS
StdVgaRestartFromTimeout(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx);

NTSTATUS
StdVgaCollectDbgInfo(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo);

NTSTATUS
StdVgaAcquireSwizzlingRange(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                            _Inout_ DXGKARG_ACQUIRESWIZZLINGRANGE *pAcquireSwizzlingRange);

NTSTATUS
StdVgaReleaseSwizzlingRange(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx,
                            _In_ CONST DXGKARG_RELEASESWIZZLINGRANGE *pReleaseSwizzlingRange);
