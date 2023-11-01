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
#include "helper.h"
#include "baseobj.h"
#include "viogpu_adapter.h"
#include "viogpu_device.h"
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
    nDebugLevel = TRACE_LEVEL_WARNING;
#if defined(COM_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncSerial;
#elif defined(PRINT_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncKdPrint;
#endif
}
#endif

#include <ntddk.h>
#include "viogpu_device.h"

#pragma code_seg(push)
#pragma code_seg("PAGE")
extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    PAGED_CODE();
    WPP_INIT_TRACING(pDriverObject, pRegistryPath)
    DbgPrint(TRACE_LEVEL_FATAL, ("---> VIOGPU FULL build on on %s %s\n", __DATE__, __TIME__));
    DRIVER_INITIALIZATION_DATA InitialData = { 0 };


    InitialData.Version = DXGKDDI_INTERFACE_VERSION_WDDM1_3;
    
    InitialData.DxgkDdiAddDevice = VioGpu3DAddDevice;
    InitialData.DxgkDdiStartDevice = VioGpu3DStartDevice;
    InitialData.DxgkDdiStopDevice = VioGpu3DStopDevice;
    InitialData.DxgkDdiRemoveDevice = VioGpu3DRemoveDevice;

    InitialData.DxgkDdiDispatchIoRequest = VioGpu3DDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine = VioGpu3DInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine = VioGpu3DDpcRoutine;

    InitialData.DxgkDdiQueryChildRelations = VioGpu3DQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus = VioGpu3DQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor = VioGpu3DQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState = VioGpu3DSetPowerState;
    InitialData.DxgkDdiResetDevice = VioGpu3DResetDevice;
    InitialData.DxgkDdiUnload = VioGpu3DUnload;

    InitialData.DxgkDdiQueryAdapterInfo = VioGpu3DQueryAdapterInfo;
    InitialData.DxgkDdiEscape = VioGpu3DEscape;
    InitialData.DxgkDdiCreateAllocation = VioGpu3DCreateAllocation;
    InitialData.DxgkDdiOpenAllocation = VioGpu3DOpenAllocation;
    InitialData.DxgkDdiCloseAllocation = VioGpu3DCloseAllocation;
    InitialData.DxgkDdiDescribeAllocation = VioGpu3DDescribeAllocation;
    InitialData.DxgkDdiDestroyAllocation = VioGpu3DDestroyAllocation;
    InitialData.DxgkDdiGetStandardAllocationDriverData = VioGpu3DGetStandardAllocationDriverData;
    InitialData.DxgkDdiBuildPagingBuffer = VioGpu3DBuildPagingBuffer;

    InitialData.DxgkDdiCreateContext = VioGpu3DDdiCreateContext;
    InitialData.DxgkDdiDestroyContext = VioGpu3DDdiDestroyContext;

    InitialData.DxgkDdiPresent = VioGpu3DPresent;
    InitialData.DxgkDdiRender = VioGpu3DRender;
    InitialData.DxgkDdiPatch = VioGpu3DPatch;
    InitialData.DxgkDdiSubmitCommand = VioGpu3DSubmitCommand;

    InitialData.DxgkDdiSetPointerPosition = VioGpu3DSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape = VioGpu3DSetPointerShape;
    InitialData.DxgkDdiIsSupportedVidPn = VioGpu3DIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn = VioGpu3DRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality = VioGpu3DEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility = VioGpu3DSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn = VioGpu3DCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = VioGpu3DUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiSetVidPnSourceAddress = VioGpu3DSetVidPnSourceAddress;
    InitialData.DxgkDdiRecommendMonitorModes = VioGpu3DRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability = VioGpu3DQueryVidPnHWCapability;
    InitialData.DxgkDdiSystemDisplayEnable = VioGpu3DSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite = VioGpu3DSystemDisplayWrite;

    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = VioGpu3DStopDeviceAndReleasePostDisplayOwnership;

    InitialData.DxgkDdiCreateDevice = VioGpu3DCreateDevice;
    InitialData.DxgkDdiDestroyDevice = VioGpu3DDestroyDevice;

    InitialData.DxgkDdiPreemptCommand = VioGpu3DDdiPreemptCommand;
    InitialData.DxgkDdiResetFromTimeout = VioGpu3DDdiResetFromTimeout;
    InitialData.DxgkDdiRestartFromTimeout = VioGpu3DDdiRestartFromTimeout;
    InitialData.DxgkDdiCollectDbgInfo = VioGpu3DDdiCollectDbgInfo;
    InitialData.DxgkDdiQueryCurrentFence = VioGpu3DDdiQueryCurrentFence;

    InitialData.DxgkDdiQueryEngineStatus = VioGpu3DDdiQueryEngineStatus;
    InitialData.DxgkDdiResetEngine = VioGpu3DDdiResetEngine;
    InitialData.DxgkDdiCancelCommand = VioGpu3DDdiCancelCommand;

    InitialData.DxgkDdiGetNodeMetadata = VioGpu3DDdiGetNodeMetadata;
    InitialData.DxgkDdiControlInterrupt = VioGpu3DDdiControlInterrupt;
    InitialData.DxgkDdiGetScanLine = VioGpu3DDdiGetScanLine;

    NTSTATUS Status = DxgkInitialize(pDriverObject, pRegistryPath, &InitialData);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkInitialize failed with Status: 0x%X\n", Status));
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
VioGpu3DUnload(VOID)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s\n", __FUNCTION__));
    WPP_CLEANUP(NULL);
}

NTSTATUS
VioGpu3DAddDevice(
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

    VioGpuAdapter* pAdapter = new(NonPagedPoolNx) VioGpuAdapter(pPhysicalDeviceObject);
    if (pAdapter == NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pAdapter failed to be allocated"));
        return STATUS_NO_MEMORY;
    }

    *ppDeviceContext = pAdapter;

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s ppDeviceContext = %p\n", __FUNCTION__, pAdapter));
    return STATUS_SUCCESS;
}

NTSTATUS
VioGpu3DRemoveDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, pDeviceContext));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);

    if (pAdapter)
    {
        delete pAdapter;
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
VioGpu3DStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
}

NTSTATUS
VioGpu3DStopDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->StopDevice();
}


NTSTATUS
VioGpu3DDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VioGpuAdapter (0x%I64x) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->DispatchIoRequest(VidPnSourceId, pVideoRequestPacket);
}

NTSTATUS
VioGpu3DSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    if (!pAdapter->IsDriverActive())
    {
        return STATUS_SUCCESS;
    }
    return pAdapter->SetPowerState(HardwareUid, DevicePowerState, ActionType);
}

NTSTATUS
VioGpu3DQueryChildRelations(
    _In_  VOID*              pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_  ULONG              ChildRelationsSize)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->QueryChildRelations(pChildRelations, ChildRelationsSize);
}

NTSTATUS
VioGpu3DQueryChildStatus(
    _In_    VOID*            pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN          NonDestructiveOnly)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->QueryChildStatus(pChildStatus, NonDestructiveOnly);
}

NTSTATUS
VioGpu3DQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("VIOGPU (%p) is being called when not active!", pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->QueryDeviceDescriptor(ChildUid, pDeviceDescriptor);
}


NTSTATUS
APIENTRY
VioGpu3DQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    return pAdapter->QueryAdapterInfo(pQueryAdapterInfo);
}


NTSTATUS 
APIENTRY
VioGpu3DDdiGetNodeMetadata(
    _In_ CONST HANDLE               hAdapter,
    UINT                            NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA*  pGetNodeMetadata) {
    PAGED_CODE(); 
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__)); 

    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);

    pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    pGetNodeMetadata->Flags.Value = 0;

    return STATUS_SUCCESS;
};


NTSTATUS
APIENTRY
VioGpu3DSetPointerPosition(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpu (%p) is being called when not active!", pAdapter));
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
VioGpu3DSetPointerShape(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
VioGpu3DEscape(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_ESCAPE*        pEscape
    )
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->Escape(pEscape);
}

NTSTATUS
APIENTRY
VioGpu3DCreateAllocation(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION* pCreateAllocation
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return VioGpuAllocation::DxgkCreateAllocation(pAdapter, pCreateAllocation);
}


NTSTATUS
APIENTRY
VioGpu3DDescribeAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAllocation* pAllocation = reinterpret_cast<VioGpuAllocation*>(pDescribeAllocation->hAllocation);
    VIOGPU_ASSERT_CHK(pAllocation != NULL);

    return pAllocation->DescribeAllocation(pDescribeAllocation);
}

NTSTATUS
APIENTRY
VioGpu3DOpenAllocation(
    _In_ CONST HANDLE                  hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION* pOpenAllocation
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hDevice != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDevice* pDxContext = reinterpret_cast<VioGpuDevice*>(hDevice);
    return pDxContext->OpenAllocation(pOpenAllocation);
}

NTSTATUS
APIENTRY
VioGpu3DCloseAllocation(
    _In_ CONST HANDLE                   hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION* pCloseAllocation
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hDevice != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    for (ULONG i = 0; i < pCloseAllocation->NumAllocations; i++) {
        VioGpuDeviceAllocation* allocation = reinterpret_cast<VioGpuDeviceAllocation*>(pCloseAllocation->pOpenHandleList[i]);
        if (allocation != NULL) {
            delete allocation;
        }
    }

    return STATUS_SUCCESS;
}


NTSTATUS
APIENTRY
VioGpu3DDestroyAllocation(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION*     pDestroyAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    VIOGPU_ASSERT_CHK(pDestroyAllocation != NULL);
    
    for (ULONG i = 0; i < pDestroyAllocation->NumAllocations; i++) {
        VioGpuAllocation* allocation = reinterpret_cast<VioGpuAllocation*>(pDestroyAllocation->pAllocationList[i]);
        if (allocation != NULL) {
            delete allocation;
        }
    }

    if (pDestroyAllocation->Flags.DestroyResource) {
        VioGpuResource* resource = reinterpret_cast<VioGpuResource*>(pDestroyAllocation->hResource);
        if (resource != NULL) {
            delete resource;
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s \n", __FUNCTION__));
    return STATUS_SUCCESS;
}



NTSTATUS
APIENTRY
VioGpu3DGetStandardAllocationDriverData(
    _In_ CONST HANDLE                                hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pStandardAllocation
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    return VioGpuAllocation::GetStandardAllocationDriverData(pStandardAllocation);
}


NTSTATUS
APIENTRY
VioGpu3DBuildPagingBuffer(
    _In_ CONST HANDLE                 hAdapter,
    _In_ DXGKARG_BUILDPAGINGBUFFER*   pBuildPagingBuffer
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    VIOGPU_ASSERT(pBuildPagingBuffer != NULL);
    
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s operation=%d\n", __FUNCTION__, pBuildPagingBuffer->Operation));

    switch (pBuildPagingBuffer->Operation)
    {
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    {
        if (pBuildPagingBuffer->MapApertureSegment.hAllocation == NULL) {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s (map aperture segment) no allocation specified\n", __FUNCTION__));
            return STATUS_SUCCESS;
        }

        VioGpuAllocation* allocation = reinterpret_cast<VioGpuAllocation*>(pBuildPagingBuffer->MapApertureSegment.hAllocation);
        NTSTATUS Status = allocation->MapApertureSegment(pBuildPagingBuffer);
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s (map aperture segment)\n", __FUNCTION__));
        return Status;
    }
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
    {
        if (pBuildPagingBuffer->UnmapApertureSegment.hAllocation == NULL) {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s (map aperture segment) no allocation specified\n", __FUNCTION__));
            return STATUS_SUCCESS;
        }

        VioGpuAllocation* allocation = reinterpret_cast<VioGpuAllocation*>(pBuildPagingBuffer->UnmapApertureSegment.hAllocation);
        NTSTATUS Status = allocation->UnmapApertureSegment(pBuildPagingBuffer);
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s (unmap aperture segment)\n", __FUNCTION__));
        return Status;
    }
    default: {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s (unknown operation %d)\n", __FUNCTION__, pBuildPagingBuffer->Operation));
        return STATUS_NOT_SUPPORTED;
    }
    };
}



NTSTATUS
APIENTRY
VioGpu3DPatch(
    _In_ CONST HANDLE         hAdapter,
    _In_ CONST DXGKARG_PATCH* pPatch)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->commander.Patch(pPatch);
};

NTSTATUS
APIENTRY
VioGpu3DSubmitCommand(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND*     pSubmitCommand) 
{
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    //DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    //DbgPrint(TRACE_LEVEL_ERROR, ("Fake imp %s\n", __FUNCTION__)); 

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        //DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->commander.SubmitCommand(pSubmitCommand);
};



NTSTATUS
APIENTRY
VioGpu3DCreateDevice(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE*     pCreateDevice
)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    
    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s VioGpu (%p) is being called when not active!\n", __FUNCTION__, pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    

    pCreateDevice->hDevice = new (NonPagedPoolNx)VioGpuDevice(pAdapter);
    if (!pCreateDevice->hDevice)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s failed to allocate VioGpuDevice\n", __FUNCTION__));
        return STATUS_NO_MEMORY;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
APIENTRY
VioGpu3DDestroyDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, pDeviceContext));

    VioGpuDevice* pDxContext = reinterpret_cast<VioGpuDevice*>(pDeviceContext);

    if (pDxContext)
    {
        delete pDxContext;
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS
APIENTRY
VioGpu3DDdiCreateContext(
    _In_    CONST HANDLE             hDevice,
    _Inout_ DXGKARG_CREATECONTEXT    *pCreateContext) {
    PAGED_CODE(); 
    
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__)); 

    // We currently don't have sepraration between context and device
    pCreateContext->hContext = hDevice;
    
    pCreateContext->ContextInfo.DmaBufferSegmentSet = 0;
    pCreateContext->ContextInfo.DmaBufferSize = 256 * 1024;
    pCreateContext->ContextInfo.DmaBufferPrivateDataSize = 40;

    pCreateContext->ContextInfo.AllocationListSize = DXGK_ALLOCATION_LIST_SIZE_GDICONTEXT;
    pCreateContext->ContextInfo.PatchLocationListSize = DXGK_ALLOCATION_LIST_SIZE_GDICONTEXT;

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiDestroyContext(
    _In_ CONST HANDLE     hContext) {
    PAGED_CODE(); 
    
    UNREFERENCED_PARAMETER(hContext);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__)); 
    
    return STATUS_SUCCESS;
};



NTSTATUS
APIENTRY
VioGpu3DPresent(
    _In_ CONST HANDLE              hDevice,
    _Inout_   DXGKARG_PRESENT*     pPresent)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hDevice != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDevice* pDxContext = reinterpret_cast<VioGpuDevice*>(hDevice);
    return pDxContext->Present(pPresent);
}

NTSTATUS
APIENTRY
VioGpu3DRender(
    _In_ CONST HANDLE             hDevice,
    _Inout_   DXGKARG_RENDER*     pRender)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hDevice != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuDevice* pDxContext = reinterpret_cast<VioGpuDevice*>(hDevice);
    return pDxContext->Render(pRender);
}

NTSTATUS
APIENTRY
VioGpu3DStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    if (pAdapter)
    {
        status = pAdapter->StopDeviceAndReleasePostDisplayOwnership(TargetId, DisplayInfo);
    }
    return status;
}

NTSTATUS
APIENTRY
VioGpu3DIsSupportedVidPn(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("VIOGPU (%p) is being called when not active!", pAdapter));
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.IsSupportedVidPn(pIsSupportedVidPn);
}

NTSTATUS
APIENTRY
VioGpu3DRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.RecommendFunctionalVidPn(pRecommendFunctionalVidPn);
}

NTSTATUS
APIENTRY
VioGpu3DRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.RecommendVidPnTopology(pRecommendVidPnTopology);
}

NTSTATUS
APIENTRY
VioGpu3DRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.RecommendMonitorModes(pRecommendMonitorModes);
}

NTSTATUS
APIENTRY
VioGpu3DEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.EnumVidPnCofuncModality(pEnumCofuncModality);
}

NTSTATUS
APIENTRY
VioGpu3DSetVidPnSourceVisibility(
    _In_ CONST HANDLE                            hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.SetVidPnSourceVisibility(pSetVidPnSourceVisibility);
}

NTSTATUS
APIENTRY
VioGpu3DCommitVidPn(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.CommitVidPn(pCommitVidPn);
}

NTSTATUS
APIENTRY
VioGpu3DUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                      hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);
}

NTSTATUS
APIENTRY
VioGpu3DQueryVidPnHWCapability(
    _In_ CONST HANDLE                       hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    VIOGPU_ASSERT_CHK(hAdapter != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    if (!pAdapter->IsDriverActive())
    {
        VIOGPU_LOG_ASSERTION1("VIOGPU (%p) is being called when not active!", pAdapter);
        return STATUS_UNSUCCESSFUL;
    }
    return pAdapter->vidpn.QueryVidPnHWCapability(pVidPnHWCaps);
}

NTSTATUS
APIENTRY
VioGpu3DDdiControlInterrupt(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE    InterruptType,
    _In_ BOOLEAN                      EnableInterrupt) {
    PAGED_CODE(); 
    
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptType);
    UNREFERENCED_PARAMETER(EnableInterrupt);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s not implemented\n", __FUNCTION__));
    return STATUS_SUCCESS;
};



NTSTATUS
APIENTRY
VioGpu3DDdiGetScanLine(
    _In_    CONST HANDLE         hAdapter,
    _Inout_ DXGKARG_GETSCANLINE* pGetScanLine
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetScanLine);

    DbgBreakPoint(); DbgPrint(TRACE_LEVEL_ERROR, ("Not imp %s\n", __FUNCTION__)); return STATUS_NO_MEMORY;
}


//END: Paged Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

VOID
VioGpu3DDpcRoutine(
    _In_  VOID* pDeviceContext)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    if (!pAdapter->IsHardwareInit())
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("VioGpu (%p) is being called when not active!", pAdapter));
        return;
    }
    pAdapter->DpcRoutine();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN
VioGpu3DInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->InterruptRoutine(MessageNumber);
}


NTSTATUS VioGpu3DSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress
)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(hAdapter);
    pAdapter->vidpn.SetVidPnSourceAddress(pSetVidPnSourceAddress);

    return STATUS_SUCCESS;
}

VOID
VioGpu3DResetDevice(
    _In_  VOID* pDeviceContext)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    pAdapter->ResetDevice();
}

NTSTATUS
APIENTRY
VioGpu3DSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    VIOGPU_ASSERT_CHK(pDeviceContext != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    return pAdapter->vidpn.SystemDisplayEnable(TargetId, Flags, Width, Height, ColorFormat);
}

VOID
APIENTRY
VioGpu3DSystemDisplayWrite(
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

    VioGpuAdapter* pAdapter = reinterpret_cast<VioGpuAdapter*>(pDeviceContext);
    pAdapter->vidpn.SystemDisplayWrite(Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);
}




NTSTATUS
APIENTRY
VioGpu3DDdiPreemptCommand(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND*    pPreemptCommand) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    
    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION fence_id=%d\n", __FUNCTION__, pPreemptCommand->PreemptionFenceId));
    
    return STATUS_SUCCESS;
};

NTSTATUS 
APIENTRY
VioGpu3DDdiRestartFromTimeout(
    _In_ CONST HANDLE     hAdapter) {
    UNREFERENCED_PARAMETER(hAdapter);
    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));
    
    return STATUS_SUCCESS;
};


NTSTATUS
APIENTRY
VioGpu3DDdiCancelCommand(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGKARG_CANCELCOMMAND* pCancelCommand) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiQueryCurrentFence(
    _In_    CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE*   pCurrentFence) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCurrentFence);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiResetEngine(
    _In_    CONST HANDLE          hAdapter,
    _Inout_ DXGKARG_RESETENGINE*  pResetEngine) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pResetEngine);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiQueryEngineStatus(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYENGINESTATUS*  pQueryEngineStatus) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pQueryEngineStatus);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiCollectDbgInfo(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COLLECTDBGINFO*    pCollectDbgInfo) {
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

NTSTATUS
APIENTRY
VioGpu3DDdiResetFromTimeout(
    _In_ CONST HANDLE     hAdapter) {
    UNREFERENCED_PARAMETER(hAdapter);

    DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s UNSUPPORTED PREEMPTION FUNCTION\n", __FUNCTION__));

    return STATUS_SUCCESS;
};

#if defined(DBG)

#if defined(COM_DEBUG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE    256

void DebugPrintFuncSerial(CONST char *format, ...)
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
void DebugPrintFuncKdPrint(CONST char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
    va_end(list);
}
#endif

#endif
#pragma code_seg(pop) // End Non-Paged Code

