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

#include "driver.h"
#include "viogpudo.h"
#include "helper.h"
#include "baseobj.h"

#if !DBG
#include "driver.tmh"
#endif

#pragma code_seg(push)
#pragma code_seg("INIT")

int nDebugLevel;
int virtioDebugLevel;
int bDebugPrint;
int bBreakAlways;

tDebugPrintFunc VirtioDebugPrintProc;

#ifdef DBG
void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    bDebugPrint = 0;
    virtioDebugLevel = 0;
    nDebugLevel = TRACE_LEVEL_NONE;
    bBreakAlways = 0;

    bDebugPrint = 1;
    virtioDebugLevel = 0x5;
    bBreakAlways = 1;
    nDebugLevel = TRACE_LEVEL_FATAL;
#if defined(COM_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncSerial;
#elif defined(PRINT_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncKdPrint;
#endif
}
#endif


extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    PAGED_CODE();
    WPP_INIT_TRACING(pDriverObject, pRegistryPath);

    //    VioGpuDbgBreak();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> KMDOD build on on %s %s\n", __DATE__, __TIME__));

    KMDDOD_INITIALIZATION_DATA InitialData = { 0 };

    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;//DXGKDDI_INTERFACE_VERSION;

    InitialData.DxgkDdiAddDevice = VioGpuDodAddDevice;
    InitialData.DxgkDdiStartDevice = VioGpuDodStartDevice;
    InitialData.DxgkDdiStopDevice = VioGpuDodStopDevice;
    InitialData.DxgkDdiResetDevice = VioGpuDodResetDevice;
    InitialData.DxgkDdiRemoveDevice = VioGpuDodRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest = VioGpuDodDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine = VioGpuDodInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine = VioGpuDodDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations = VioGpuDodQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus = VioGpuDodQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor = VioGpuDodQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState = VioGpuDodSetPowerState;
    InitialData.DxgkDdiUnload = VioGpuDodUnload;
    InitialData.DxgkDdiQueryAdapterInfo = VioGpuDodQueryAdapterInfo;
    InitialData.DxgkDdiSetPointerPosition = VioGpuDodSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape = VioGpuDodSetPointerShape;
    InitialData.DxgkDdiEscape = VioGpuDodEscape;
    InitialData.DxgkDdiIsSupportedVidPn = VioGpuDodIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn = VioGpuDodRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality = VioGpuDodEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility = VioGpuDodSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn = VioGpuDodCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = VioGpuDodUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiRecommendMonitorModes = VioGpuDodRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability = VioGpuDodQueryVidPnHWCapability;
    InitialData.DxgkDdiPresentDisplayOnly = VioGpuDodPresentDisplayOnly;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = VioGpuDodStopDeviceAndReleasePostDisplayOwnership;
    InitialData.DxgkDdiSystemDisplayEnable = VioGpuDodSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite = VioGpuDodSystemDisplayWrite;

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkInitializeDisplayOnlyDriver failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}
// END: Init Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("PAGE")

//
// PnP DDIs
//

VOID
VioGpuDodUnload(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s\n", __FUNCTION__));
    WPP_CLEANUP(NULL);
}

NTSTATUS
VioGpuDodAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if ((pPhysicalDeviceObject == NULL) ||
        (ppDeviceContext == NULL))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("One of pPhysicalDeviceObject (%p), ppDeviceContext (%p) is NULL",
            pPhysicalDeviceObject, ppDeviceContext));
        return STATUS_INVALID_PARAMETER;
    }
    *ppDeviceContext = NULL;

    VioGpuDod* pVioGpuDod = new(NonPagedPoolNx) VioGpuDod(pPhysicalDeviceObject);
    if (pVioGpuDod == NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pVioGpuDod failed to be allocated"));
        return STATUS_NO_MEMORY;
    }

    *ppDeviceContext = pVioGpuDod;

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s ppDeviceContext = %p\n", __FUNCTION__, pVioGpuDod));
    return STATUS_SUCCESS;
}

NTSTATUS
VioGpuDodRemoveDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, pDeviceContext));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);

    if (pVioGpuDod)
    {
        delete pVioGpuDod;
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
VioGpuDodStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
}

NTSTATUS
VioGpuDodStopDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->StopDevice();
}


NTSTATUS
VioGpuDodDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VioGpuDod (0x%I64x) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->DispatchIoRequest(VidPnSourceId, pVideoRequestPacket);
}

NTSTATUS
VioGpuDodSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    if (!pVioGpuDod->IsDriverActive())
    {
        return STATUS_SUCCESS;
    }
    return pVioGpuDod->SetPowerState(HardwareUid, DevicePowerState, ActionType);
}

NTSTATUS
VioGpuDodQueryChildRelations(
    _In_  VOID*              pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_  ULONG              ChildRelationsSize)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->QueryChildRelations(pChildRelations, ChildRelationsSize);
}

NTSTATUS
VioGpuDodQueryChildStatus(
    _In_    VOID*            pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN          NonDestructiveOnly)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->QueryChildStatus(pChildStatus, NonDestructiveOnly);
}

NTSTATUS
VioGpuDodQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    if (!pVioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("VIOGPU (%p) is being called when not active!", pVioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->QueryDeviceDescriptor(ChildUid, pDeviceDescriptor);
}


NTSTATUS
APIENTRY
VioGpuDodQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    return pVioGpuDod->QueryAdapterInfo(pQueryAdapterInfo);
}

NTSTATUS
APIENTRY
VioGpuDodSetPointerPosition(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpu (%p) is being called when not active!", pVioGpuDod));
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->SetPointerPosition(pSetPointerPosition);
}

NTSTATUS
APIENTRY
VioGpuDodSetPointerShape(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pVioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->SetPointerShape(pSetPointerShape);
}

NTSTATUS
APIENTRY
VioGpuDodEscape(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_ESCAPE*        pEscape
    )
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pVioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->Escape(pEscape);
}

NTSTATUS
APIENTRY
VioGpuDodPresentDisplayOnly(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->PresentDisplayOnly(pPresentDisplayOnly);
}

NTSTATUS
APIENTRY
VioGpuDodStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    if (pVioGpuDod)
    {
        status = pVioGpuDod->StopDeviceAndReleasePostDisplayOwnership(TargetId, DisplayInfo);
    }
    return status;
}

NTSTATUS
APIENTRY
VioGpuDodIsSupportedVidPn(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("VIOGPU (%p) is being called when not active!", pVioGpuDod));
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->IsSupportedVidPn(pIsSupportedVidPn);
}

NTSTATUS
APIENTRY
VioGpuDodRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->RecommendFunctionalVidPn(pRecommendFunctionalVidPn);
}

NTSTATUS
APIENTRY
VioGpuDodRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->RecommendVidPnTopology(pRecommendVidPnTopology);
}

NTSTATUS
APIENTRY
VioGpuDodRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->RecommendMonitorModes(pRecommendMonitorModes);
}

NTSTATUS
APIENTRY
VioGpuDodEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->EnumVidPnCofuncModality(pEnumCofuncModality);
}

NTSTATUS
APIENTRY
VioGpuDodSetVidPnSourceVisibility(
    _In_ CONST HANDLE                            hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->SetVidPnSourceVisibility(pSetVidPnSourceVisibility);
}

NTSTATUS
APIENTRY
VioGpuDodCommitVidPn(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->CommitVidPn(pCommitVidPn);
}

NTSTATUS
APIENTRY
VioGpuDodUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                      hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);
}

NTSTATUS
APIENTRY
VioGpuDodQueryVidPnHWCapability(
    _In_ CONST HANDLE                       hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(hAdapter);
    if (!pVioGpuDod->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pVioGpuDod);
        return STATUS_UNSUCCESSFUL;
    }
    return pVioGpuDod->QueryVidPnHWCapability(pVidPnHWCaps);
}

//END: Paged Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

VOID
VioGpuDodDpcRoutine(
    _In_  VOID* pDeviceContext)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    if (!pVioGpuDod->IsHardwareInit())
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("VioGpu (%p) is being called when not active!", pVioGpuDod));
        return;
    }
    pVioGpuDod->DpcRoutine();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN
VioGpuDodInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->InterruptRoutine(MessageNumber);
}

VOID
VioGpuDodResetDevice(
    _In_  VOID* pDeviceContext)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    pVioGpuDod->ResetDevice();
}

NTSTATUS
APIENTRY
VioGpuDodSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    return pVioGpuDod->SystemDisplayEnable(TargetId, Flags, Width, Height, ColorFormat);
}

VOID
APIENTRY
VioGpuDodSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    VioGpuDod* pVioGpuDod = reinterpret_cast<VioGpuDod*>(pDeviceContext);
    pVioGpuDod->SystemDisplayWrite(Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);
}

#if defined(DBG)

#if defined(COM_DEBUG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE    256

void DebugPrintFuncSerial(const char *format, ...)
{
    char buf[TEMP_BUFFER_SIZE];
    NTSTATUS status;
    size_t len;
    va_list list;
    va_start(list, format);
    status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
    if (status == STATUS_SUCCESS)
    {
        len = strlen(buf);
    }
    else
    {
        len = 2;
        buf[0] = 'O';
        buf[1] = '\n';
    }
    if (len)
    {
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)buf, (ULONG)len);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
    }
    va_end(list);
}
#endif

#if defined(PRINT_DEBUG)
void DebugPrintFuncKdPrint(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
    va_end(list);
}
#endif

#endif
#pragma code_seg(pop) // End Non-Paged Code

