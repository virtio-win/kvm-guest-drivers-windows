/*
 * This file contains virtio-mem driver routines
 *
 * Copyright (c) 2022-2024  Red Hat, Inc.
 *
 * Author(s):
 *  Marek Kedzierski <mkedzier@redhat.com>
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
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "viomem.tmh"
#endif

NTSTATUS
ViomemInit(IN WDFOBJECT    WdfDevice)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    u64 u64HostFeatures;
    u64 u64GuestFeatures = 0;
    bool notify_stat_queue = false;
    VIRTIO_WDF_QUEUE_PARAM params[3];
    virtio_mem_config configReqest = { 0 };
    PVIOQUEUE vqs[3];
    ULONG nvqs;
    
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

    WdfObjectAcquireLock(WdfDevice);

	//
	// Read features offered by the virtio-mem. 
	//

	u64HostFeatures = VirtIOWdfGetDeviceFeatures(&devCtx->VDevice);

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,
		"VirtIO device features: %I64X\n", u64HostFeatures);
	
	//
	// Currently, two features are supported:
	// VIRTIO_MEM_F_ACPI_PXM and VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE
	// 
	// VIRTIO_MEM_F_ACPI_PXM is related to NUMA per node memory
	// plug / unplug support.Windows' physical memory add function doesn't
	// provide a way to specify the node number. The driver informs that
	// this feature is supported because the Windows memory manager
	// implicitly makes decisions about nodes based on memory ACPI configuration.
	//

	if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_MEM_F_ACPI_PXM))
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
			"VIRTIO_MEM_F_ACPI_PXM enabled, virtio-mem is NUMA aware\n");

		virtio_feature_enable(u64GuestFeatures, VIRTIO_MEM_F_ACPI_PXM);
		devCtx->ACPIProximityIdActive = TRUE;
	}

	//
	// According to the official virtio-mem specs, VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE
	// support requires unplugged (removed) memory not to be accessed.
    //
	// This requirement is met because:
	// 
	// - The driver doesn't access the removed memory. 
	// - The Windows memory manager guarantees that removed memory will not 
	//   be accessed, even when the OS runs under Hyper-V as root partition
	//   (proved by empirical study).
	//

	if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE))
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
			"VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE enabled\n");

		virtio_feature_enable(u64GuestFeatures, VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE);
	}

	status = VirtIOWdfSetDriverFeatures(&devCtx->VDevice, u64GuestFeatures, 0);
	if (NT_SUCCESS(status))
	{
		params[0].Interrupt = devCtx->WdfInterrupt;
		nvqs = 1;
		status = VirtIOWdfInitQueues(&devCtx->VDevice, nvqs, vqs, params);
		if (NT_SUCCESS(status))
		{
			devCtx->infVirtQueue = vqs[0];

			// 
			// If bitmap buffer representing memory blocks is empty, 
			// it means that we are starting, so we have to allocate memory 
			// for the bitmap representing memory blocks (memory region)
			//
			VirtIOWdfDeviceGet(&devCtx->VDevice, 0, &configReqest, sizeof(configReqest));

			//
			// Calculate the size of bitmap representing memory region and 
			// try to allocate memory for the bitmap.
			//
			// Note: each bit represents one block of memory where size of block 
			//	     is equal to block_size field of VIRTIO_MEM_CONFIG.
			//
			ULONG bitmapSizeInBits = (ULONG)(configReqest.region_size / configReqest.block_size);

			devCtx->bitmapBuffer = (ULONG*)ExAllocatePoolZero(NonPagedPool,
				bitmapSizeInBits >> 3,
				VIRTIO_MEM_POOL_TAG);

			//
			// If the memory was allocated init bitmap: assign the bitmap buffer to 
			// handle and then reset bitmap bits to zero.
			// If operation failed then try to add an event informing about failure and
			// fail the driver initialization.
			//
			if (devCtx->bitmapBuffer != NULL)
			{
				RtlInitializeBitMap(&devCtx->memoryBitmapHandle, devCtx->bitmapBuffer,
					bitmapSizeInBits);

				RtlClearAllBits(&devCtx->memoryBitmapHandle);
				
				VirtIOWdfSetDriverOK(&devCtx->VDevice);
			}
			else
			{
				TraceEvents(TRACE_LEVEL_FATAL, DBG_PNP, "Can't allocate memory for memory bitmap!\n");

				status = STATUS_INSUFFICIENT_RESOURCES;

				VirtIOWdfSetDriverFailed(&devCtx->VDevice);
			}
		}
		else
		{
			TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitQueues failed with %x\n", status);
			VirtIOWdfSetDriverFailed(&devCtx->VDevice);
		}
	}
	else
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfSetDriverFeatures failed with %x\n", status);
		VirtIOWdfSetDriverFailed(&devCtx->VDevice);
	}

    WdfObjectReleaseLock(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return\n", __FUNCTION__);
    return status;
}

VOID ViomemTerminate(IN WDFOBJECT    WdfDevice)
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

    WdfObjectAcquireLock(WdfDevice);

    VirtIOWdfDestroyQueues(&devCtx->VDevice);

    WdfObjectReleaseLock(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return\n", __FUNCTION__);
}

//
// Function used for displaying debugging responses from the device.
//

VOID inline DumpViomemResponseType(virtio_mem_resp	*MemoryResponse)
{
	switch (MemoryResponse->type)
	{
	case VIRTIO_MEM_RESP_ACK:
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIRTIO_MEM_RESP_ACK \n");
		break;
	case VIRTIO_MEM_RESP_NACK:
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIRTIO_MEM_RESP_NACK \n");
		break;
	case VIRTIO_MEM_RESP_BUSY:
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIRTIO_MEM_RESP_BUSY \n");
		break;
	case VIRTIO_MEM_RESP_ERROR:
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIRTIO_MEM_RESP_ERROR \n");
		break;
	default:
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "UKNOWN memory response \n");
	}
}

//
// Function sends VIRTIO_MEM_REQ_UNPLUG_ALL request to a device.
//	
//	Arguments: WdfDevice:		device
//			  													
//  Return value: TRUE if all memory ranges were unplugged (device returned ACK
//				  and plugged_size was set to zero)
//				  FALSE if timeout occurred or device returned an error code or 
//				  device returned ACK but plugged_size was not set to zero.
//

BOOLEAN SendUnplugAllRequest(IN WDFOBJECT WdfDevice)
{
	PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
	VIO_SG              sg[2];
	BOOLEAN				do_notify = FALSE;
	NTSTATUS            status;
	PVOID buffer;
	UINT len;
	INT result = 0;
	virtio_mem_config configuration;

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

	//
	// Fill unplug request and response command buffers with zeros before submission.
	//

	memset(devCtx->MemoryResponse, 0, sizeof(virtio_mem_resp));
	memset(devCtx->plugRequest, 0, sizeof(virtio_mem_req));

	//
	// Build UNPLUG request command.
	//

	devCtx->plugRequest->type = VIRTIO_MEM_REQ_UNPLUG_ALL;

	sg[0].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->plugRequest);
	sg[0].length = sizeof(virtio_mem_req);


	sg[1].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->MemoryResponse);
	sg[1].length = sizeof(virtio_mem_resp);

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	result = virtqueue_add_buf(devCtx->infVirtQueue, sg, 1, 1, devCtx, NULL, 0);

	if (result < 0)
	{
		WdfSpinLockRelease(devCtx->infVirtQueueLock);

		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
			"%s: Cannot add buffer = [0x%x]\n", __FUNCTION__, result);
		return FALSE;
	}
	else
	{
		do_notify = virtqueue_kick_prepare(devCtx->infVirtQueue);
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

	if (do_notify)
	{
		virtqueue_notify(devCtx->infVirtQueue);
	}

	//
	// Wait indefinitely for the device's response.
	//

	status = KeWaitForSingleObject(
		&devCtx->hostAcknowledge,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s KeWaitForSingleObject failed!\n", 
			__FUNCTION__);
		
		return FALSE;
	}

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	if (virtqueue_has_buf(devCtx->infVirtQueue))
	{
		// 
		// Remove buffer from the virtio queue.
		//

		buffer = virtqueue_get_buf(devCtx->infVirtQueue, &len);
		if (buffer)
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
				"%s Buffer got, len = [%d]!\n", __FUNCTION__, len);
		}
		else
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
				"%s Buffer got, len = [%d]!\n", __FUNCTION__, len);

			WdfSpinLockRelease(devCtx->infVirtQueueLock);
			return FALSE;
		}
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

#ifdef __DUMP_RESPONSE__
	DumpViomemResponseType(devCtx->MemoryResponse);
#endif

	if (devCtx->MemoryResponse->type == VIRTIO_MEM_RESP_ACK)
	{
		VirtIOWdfDeviceGet(&devCtx->VDevice, 0, &configuration, 
			sizeof(virtio_mem_config));
		
		if (configuration.plugged_size != 0)
		{
			TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
				"%s after VIRTIO_MEM_REQ_UNPLUG_ALL plugged_size is NOT 0!\n", 
				__FUNCTION__);

			return FALSE;
		}
		return TRUE;
	}
	else
	{

		TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
			"%s sending VIRTIO_MEM_REQ_UNPLUG_ALL failed!\n", 
			__FUNCTION__);

	}

	return FALSE;
}

//
// Function sends VIRTIO_MEM_REQ_UNPLUG request to a device.
//	
//	Arguments: WdfDevice: device
//			   Address:			address of memory block to unplug
//			   NumberOfBlocks:	a number of contiguous blocks starting 
//                              at Address  
//								
//  Return value: TRUE if plug operation finished with success
//				  FALSE if timeout occurred or device returned an error code
//

BOOLEAN SendUnPlugRequest(
	IN WDFOBJECT WdfDevice,
	__virtio64 Address,
	__virtio16 NumberOfBlocks)
{
	PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
	VIO_SG              sg[2];
	BOOLEAN				do_notify = FALSE;
	NTSTATUS            status;
	PVOID buffer;
	UINT len;
	INT result = 0;

	//
	// Fill unplug request and response command buffers with zeros before submission.
	//

	memset(devCtx->MemoryResponse, 0, sizeof(virtio_mem_resp));
	memset(devCtx->plugRequest, 0, sizeof(virtio_mem_req));

	//
	// Build UNPLUG request command.
	//

	devCtx->plugRequest->type = VIRTIO_MEM_REQ_UNPLUG;
	devCtx->plugRequest->u.plug.addr = Address;
	devCtx->plugRequest->u.plug.nb_blocks = NumberOfBlocks;

	sg[0].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->plugRequest);
	sg[0].length = sizeof(virtio_mem_req);


	sg[1].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice, 
		devCtx->MemoryResponse);
	sg[1].length = sizeof(virtio_mem_resp);

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	result = virtqueue_add_buf(devCtx->infVirtQueue, sg, 1, 1, devCtx, NULL, 0);

	if (result < 0)
	{
		WdfSpinLockRelease(devCtx->infVirtQueueLock);

		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, 
			"%s: Cannot add buffer = [0x%x]\n", __FUNCTION__, result);
		return FALSE;
	}
	else
	{
		do_notify = virtqueue_kick_prepare(devCtx->infVirtQueue);
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

	if (do_notify)
	{
		virtqueue_notify(devCtx->infVirtQueue);
	}

	//
	// Wait indefinitely for the device's response.
	//

	status = KeWaitForSingleObject(
		&devCtx->hostAcknowledge,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s KeWaitForSingleObject failed!\n",
			__FUNCTION__);

		return FALSE;
	}

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	if (virtqueue_has_buf(devCtx->infVirtQueue))
	{
		// 
		// Remove buffer from the virtio queue.
		//

		buffer = virtqueue_get_buf(devCtx->infVirtQueue, &len);
		if (buffer)
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, 
				"%s Buffer got, len = [%d]!\n", __FUNCTION__, len);
		}
		else
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
				"%s Buffer got, len = [%d]!\n", __FUNCTION__, len);

			WdfSpinLockRelease(devCtx->infVirtQueueLock);
			return FALSE;
		}
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

#if 0
	DumpViomemResponseType(devCtx->MemoryResponse);
#endif

	if (devCtx->MemoryResponse->type == VIRTIO_MEM_RESP_ACK)
	{
		return TRUE;
	}

	return FALSE;
}

//
// Function sends VIRTIO_MEM_REQ_PLUG request to a device.
//	
//	Arguments: Address - address of memory block to plug
//			   NumberOfBlocks - a number of contiguous blocks starting 
//                             at Address  
//								
//  Return value: TRUE if plug operation finished with success
//				  FALSE if timeout occured or device returned an error code
//

BOOLEAN SendPlugRequest(
	IN WDFOBJECT WdfDevice,
	__virtio64 Address,
	__virtio16 NumberOfBlocks)
{
	VIO_SG              sg[2];
	bool                doNotify;
	NTSTATUS            status;
	PVOID               buffer;
	UINT len;

	PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
	
	//
	// Fill plug request and response command buffers with zeros before submission.
	//

	memset(devCtx->MemoryResponse, 0, sizeof(virtio_mem_resp));
	memset(devCtx->plugRequest, 0, sizeof(virtio_mem_req));

	//
	// Build PLUG request command.
	//

	devCtx->plugRequest->type = VIRTIO_MEM_REQ_PLUG;
	devCtx->plugRequest->u.plug.addr = Address;
	devCtx->plugRequest->u.plug.nb_blocks = NumberOfBlocks;

	//
	// Build scatter-gather list for plug request and response.
	// 

	sg[0].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->plugRequest);

	sg[0].length = sizeof(virtio_mem_req);

	sg[1].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->MemoryResponse);

	sg[1].length = sizeof(virtio_mem_resp);

	//
	// Under spin lock add prepared request and response to the virtio queue. After
	// preparation release the spin lock and notify queue about transmission request.
	//

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	if (virtqueue_add_buf(devCtx->infVirtQueue, sg, 1, 1, devCtx, NULL, 0) < 0)
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s Cannot add buffer\n", __FUNCTION__);
	}
	doNotify = virtqueue_kick_prepare(devCtx->infVirtQueue);
	WdfSpinLockRelease(devCtx->infVirtQueueLock);

	if (doNotify)
	{
		virtqueue_notify(devCtx->infVirtQueue);
	}

	//
	// Wait indefinitely for the device's response.
	//

	status = KeWaitForSingleObject(
		&devCtx->hostAcknowledge,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s KeWaitForSingleObject failed!\n",
			__FUNCTION__);

		return FALSE;
	}

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	if (virtqueue_has_buf(devCtx->infVirtQueue))
	{
		// 
		// Remove buffer from the queue.
		//

		buffer = virtqueue_get_buf(devCtx->infVirtQueue, &len);
		if (buffer)
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Buffer got, len = [%d]!\n", len);
		}
	}
	WdfSpinLockRelease(devCtx->infVirtQueueLock);

#if 0
	DumpViomemResponseType(devCtx->MemoryResponse);
#endif

	if (devCtx->MemoryResponse->type == VIRTIO_MEM_RESP_ACK)
	{
		return TRUE;
	}
	
	return FALSE;
}

//
// Function traverses MDL and returns number of consecutive pages.
// The function is used by GetMemoryRangesFromMdl.
//

LONGLONG FindConsecutivePagesCountMDL(PPFN_NUMBER Pages, LONGLONG Remaining)
{
	PFN_NUMBER start = Pages[0];
	ULONG index = 1;

	//
	// Calculate and return a number of consecutive PFNs.
	//

	while (index < Remaining)
	{
		if (Pages[index] != (start + index))
			break;

		index++;
	}

	return index;
}

//
// Function converts MDL returned by MmAllocateNodePagesForMdlEx call to memory 
// ranges.
//

ULONG GetMemoryRangesFromMdl(PMDL Mdl, PHYSICAL_MEMORY_RANGE MemoryRanges[])
{
	PPFN_NUMBER pfnNumbers = 0;
	ULONG i = 0;
	ULONG memoryBlockIndex = 0;

	//
	// Calculate a number of PFNs from the MDL.
	//

	LONGLONG pagesToScan = Mdl->ByteCount >> PAGE_SHIFT;

	//
	// Initialize starting position of a block and a number of consecutive 
	// pages.
	//

	LONGLONG blockStartPosition = 0;
	LONGLONG consecutivePagesCount = 0;

	// Get a pointer to PFNs.

	pfnNumbers = MmGetMdlPfnArray(Mdl);

	// 
	// Form memory ranges from arithmetics sequences of pages. 
	//

	while (pagesToScan > 0)
	{
		//
		// Mark start of memory range and then scan for sequence of consecutive
		// pages.
		//

		consecutivePagesCount = FindConsecutivePagesCountMDL(&pfnNumbers[blockStartPosition],
			pagesToScan);

		//
		// Calculate both physical address of memory range and size. Add block to a list 
		// of blocks and update memory block index. 
		//

		MemoryRanges[memoryBlockIndex].BaseAddress.QuadPart =
			pfnNumbers[blockStartPosition] << PAGE_SHIFT;

		MemoryRanges[memoryBlockIndex].NumberOfBytes.QuadPart =
			consecutivePagesCount << PAGE_SHIFT;

		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
			"Found block at address 0x%.8I64x of size 0x%.8I64x\n",
			MemoryRanges[memoryBlockIndex].BaseAddress.QuadPart,
			MemoryRanges[memoryBlockIndex].NumberOfBytes.QuadPart);

		memoryBlockIndex++;

		//
		// Update next block's start position and number of remaining pages to
		// check.
		//

		blockStartPosition += consecutivePagesCount;
		pagesToScan = pagesToScan - consecutivePagesCount;
	}

	// 
	// Return number of memory ranges found.
	//

	return memoryBlockIndex;
}

//
// Function returns number of megabytes allocated in bitmap representing
// region_size
//

ULONG GetNumberOfMBytesAllocatedInBitmap(RTL_BITMAP *Bitmap, ULONG BlockSize)
{
	ULONG number = RtlNumberOfSetBits(Bitmap);
	number = (number * BlockSize) / 1048576;
	return number;
}

//
// Function dumps memory ranges allocated in bitmap.
//

void DumpBitmapMemoryRanges(LONGLONG BaseAddress,
	RTL_BITMAP *Bitmap,
	LONGLONG BlockSize)
{
	PHYSICAL_MEMORY_RANGE range;
	LONGLONG rangeStartIndex = 0;
	ULONG bitsNumberToScan = Bitmap->SizeOfBitMap;
	ULONG previousBitValue = RtlCheckBit(Bitmap, 0);
	ULONG i = 0;
	ULONG currentBitValue = 0;

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Bitmap Memory ranges:\n");

	for (i = 1; i < bitsNumberToScan; i++)
	{
		currentBitValue = RtlCheckBit(Bitmap, i);
		if (previousBitValue != currentBitValue)
		{
			range.BaseAddress.QuadPart = BaseAddress + (rangeStartIndex * BlockSize);
			range.NumberOfBytes.QuadPart = (i - rangeStartIndex) * BlockSize;

			//
			// Display information about ranges. 'F' denotes range allocated (filled), 
			// 'E' denotes range not allocated (empty).
			//

			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "[0x%.8I64x] [0x%.8I64x] type[%c]\n",
				range.BaseAddress.QuadPart,
				range.NumberOfBytes.QuadPart,
				previousBitValue ? 'F' : 'E');

			//
			// Mark index of new memory range and update attribute of bit range.
			//

			rangeStartIndex = i;
			previousBitValue = currentBitValue;
		}
	}

	range.BaseAddress.QuadPart = BaseAddress + (rangeStartIndex * BlockSize);
	range.NumberOfBytes.QuadPart = (i - rangeStartIndex) * BlockSize;

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "[0x%.8I64x] [0x%.8I64x] type[%c]\n",
		range.BaseAddress.QuadPart,
		range.NumberOfBytes.QuadPart,
		currentBitValue ? 'F' : 'E');
}

//
// Function resizes given bitmap. Currently not used.
//

VOID ResizeBitmap(RTL_BITMAP *Bitmap, PULONG BitmapBuffer, ULONG NewSizeOfBitmap)
{
	//
	// Save previous size of bitmap.
	//

	ULONG oldSizeOfBitmap = Bitmap->SizeOfBitMap;

	//
	// Resize bitmap without discarding the underlying buffer.
	//

	RtlInitializeBitMap(Bitmap, BitmapBuffer, NewSizeOfBitmap);

	//
	// If the size of the bitmap is growing, then clear appended bitmap bits.
	//

	if (NewSizeOfBitmap > oldSizeOfBitmap)
	{
		RtlSetBits(Bitmap, oldSizeOfBitmap, NewSizeOfBitmap - oldSizeOfBitmap);
	}
}

//
// Function deallocates (sets to zero) given range in bitmap 
// representation.
//

VOID DeallocateMemoryRangeInMemoryBitmap(LONGLONG BaseAddress,
	PPHYSICAL_MEMORY_RANGE RangeToDeallocate,
	RTL_BITMAP *Bitmap,
	ULONG BlockSizeBytes)
{
	//
	// Convert the range address to bit index and range size to the number of bits in
	// the bitmap. Finally clear the bits.
	//

	LONGLONG addressOffset = RangeToDeallocate->BaseAddress.QuadPart -
		BaseAddress;

	ULONG indexOfStartingBlock = (ULONG)(addressOffset / BlockSizeBytes);
	ULONG rangeLenth = (ULONG)(RangeToDeallocate->NumberOfBytes.QuadPart / BlockSizeBytes);

	RtlClearBits(Bitmap, indexOfStartingBlock, rangeLenth);
}

//
// Helper function that dumps system memory ranges information.
// Note: after each successful memory plug or memory unplug, the system memory ranges
//       are updated so this function is for just observability purposes.
//

VOID DumpSystemMemoryRanges(LONGLONG Start, LONGLONG End)
{
	PPHYSICAL_MEMORY_RANGE ranges;
	ULONG currentRange = 0;

	ranges = MmGetPhysicalMemoryRanges();

	if (ranges)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "start          end      size \n");
		for (currentRange = 0; ranges[currentRange].NumberOfBytes.QuadPart != 0; currentRange++)
		{
			LONGLONG startAddress = ranges[currentRange].BaseAddress.QuadPart;
			LONGLONG endAddress = startAddress + (ranges[currentRange].NumberOfBytes.QuadPart - 1);

			if (startAddress >= Start && endAddress <= End)
			{
				TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "0x%.8I64x 0x%.8I64x 0x%.8I64x\n",
					startAddress,
					endAddress,
					ranges[currentRange].NumberOfBytes.QuadPart);
			}
		}

		//
		// Free memory allocated for ranges structures.
		//
		
		ExFreePool(ranges);
	}
}

//
// Function with the same name was removed from WDK by Microsoft so this is functional
// (but slow) equivalent. 
//

ULONG RtlFindLongestRunSet(IN  PRTL_BITMAP BitMapHeader, OUT PULONG StartingIndex)
{
	ULONG currentRunLength = 0;
	ULONG longestRunFound = 0;
	ULONG i = 0;
	*StartingIndex = 0;

	for (i = 0; i < BitMapHeader->SizeOfBitMap; i++)
	{
		if (RtlCheckBit(BitMapHeader, i) == TRUE)
		{
			currentRunLength++;
		}
		else
		{
			if (currentRunLength > longestRunFound)
			{
				longestRunFound = currentRunLength;
				*StartingIndex = i - currentRunLength;
			}
		}
	}

	if (currentRunLength > longestRunFound)
	{
		longestRunFound = currentRunLength;
		*StartingIndex = i - currentRunLength;
	}

	return longestRunFound;
}

//
// This function searches for a busy (used) memory range that starts at the given address and 
// is less or equal to LessOrEqualExpectedSizeInBlocks in size. If the function finds the range, 
// it fills in the physical address of the range and the range's size and returns a boolean value 
// of TRUE. Otherwise, it returns a boolean value of FALSE.
//

inline BOOLEAN FindAllocatedMemoryRangeInBitmap(RTL_BITMAP *Bitmap,
	LONGLONG BaseAddress,
	ULONGLONG BlockSize,
	ULONG LessOrEqualExpectedSizeInBlocks,
	PPHYSICAL_MEMORY_RANGE RangeFound)
{

	ULONG indexOfContiguous = 0;
	ULONG numberOfBits = RtlFindLongestRunSet(Bitmap, &indexOfContiguous);

	if (numberOfBits)
	{
		numberOfBits = min(numberOfBits, LessOrEqualExpectedSizeInBlocks);
		RangeFound->BaseAddress.QuadPart = BaseAddress + (indexOfContiguous * BlockSize);
		RangeFound->NumberOfBytes.QuadPart = numberOfBits * BlockSize;
		return TRUE;
	}

	return FALSE;
}

//
// This function searches for a free memory range that starts at the given address and is less 
// or equal to LessOrEqualExpectedSizeInBlocks in size. If the function finds the range, 
// it fills in the physical address of the range and the range's size and returns a boolean value 
// of TRUE. Otherwise, it returns a boolean value of FALSE.
//

inline BOOLEAN FindFreeMemoryRangeInBitmap(RTL_BITMAP *Bitmap,
	LONGLONG BaseAddress,
	ULONGLONG BlockSize,
	ULONG LessOrEqualExpectedSizeInBlocks,
	PPHYSICAL_MEMORY_RANGE RangeFound)
{

	ULONG indexOfContiguous = 0;
	ULONG numberOfBits = RtlFindLongestRunClear(Bitmap, &indexOfContiguous);

	if (numberOfBits)
	{
		numberOfBits = min(numberOfBits, LessOrEqualExpectedSizeInBlocks);
		RangeFound->BaseAddress.QuadPart = BaseAddress + (indexOfContiguous * BlockSize);
		RangeFound->NumberOfBytes.QuadPart = numberOfBits * BlockSize;
		return TRUE;
	}

	return FALSE;
}

//
// This function marks a range of memory as busy (used) in bitmap's memory representation.
//

inline VOID AllocateMemoryRangeInMemoryBitmap(LONGLONG BaseAddress,
	PPHYSICAL_MEMORY_RANGE RangeToAllocate,
	RTL_BITMAP *Bitmap,
	ULONG BlockSize)
{
	LONGLONG addressOffset = RangeToAllocate->BaseAddress.QuadPart -
		BaseAddress;

	ULONG indexOfStartingBlock = (ULONG)(addressOffset / BlockSize);
	ULONG rangeLenth = (ULONG)(RangeToAllocate->NumberOfBytes.QuadPart / BlockSize);

	RtlSetBits(Bitmap, indexOfStartingBlock, rangeLenth);
}

//
// Function is used to add physical memory range to the system.
//

BOOLEAN VirtioMemAddPhysicalMemory(IN WDFOBJECT Device, virtio_mem_config *Configuration)
{
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

	NTSTATUS status = STATUS_SUCCESS;
	BOOLEAN result = FALSE;
	PHYSICAL_MEMORY_RANGE range;
	PHYSICAL_ADDRESS address = { 0 };
	LARGE_INTEGER amount = { 0 };
	__u64 sizeDifference = 0;

	PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

	//
	// Calculate the amount of memory we want to add.
	//

	sizeDifference = Configuration->requested_size - Configuration->plugged_size;

	address.QuadPart = (LONGLONG)Configuration->addr;
	amount.QuadPart = (LONGLONG)sizeDifference;

	//
	// Find the free block of memory that is less or equal to the size difference.
	//

	result = FindFreeMemoryRangeInBitmap(&devCtx->memoryBitmapHandle,
		address.QuadPart,
		Configuration->block_size,
		(ULONG)(sizeDifference / Configuration->block_size),
		&range);

	if (!result)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s FindFreeMemoryRangeInBitmap failed FALSE\n", __FUNCTION__);

		return FALSE;
	}

	LONGLONG startAddress = range.BaseAddress.QuadPart;
	LONGLONG endAddress = startAddress + (range.NumberOfBytes.QuadPart - 1);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Range to add start[0x%.8I64x] end[0x%.8I64x] size[0x%.8I64x]\n",
		startAddress,
		endAddress,
		range.NumberOfBytes.QuadPart);

	//
	// Calculate number of blocks to add and send plug request to the device.
	//

	__virtio16 numberOfBlocks = (__virtio16)(range.NumberOfBytes.QuadPart / Configuration->block_size);
	__virtio64 rangeAddress = (__virtio64)range.BaseAddress.QuadPart;
	result = SendPlugRequest(Device, rangeAddress, numberOfBlocks);
		
	if (!result)
	{
		// 
		// Request failed. There is no need to update anything (as memory has 
		// not been added) so return error.
		//
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s SendPlugRequest failed FALSE\n", __FUNCTION__);

		return FALSE;
	}
	else
	{
		//
		// Request sent and accepted, so try to add the memory range.
		//

		status = MmAddPhysicalMemory(&range.BaseAddress, &range.NumberOfBytes);

		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s MmAddPhysicalMemory failed 0x%x\n", __FUNCTION__, status);
			return FALSE;
		}

		//
		// Update the memory bitmap representation to reflect the change.
		//

		AllocateMemoryRangeInMemoryBitmap(address.QuadPart,
			&range,
			&devCtx->memoryBitmapHandle,
			(ULONG)Configuration->block_size);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return TRUE\n", __FUNCTION__);

	return TRUE;
}

//
// Function used to remove physical memory range from the system.
//

BOOLEAN VirtioMemRemovePhysicalMemory(IN WDFOBJECT Device, virtio_mem_config *Configuration)
{
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

	NTSTATUS status = STATUS_SUCCESS;
	BOOLEAN result = FALSE;
	PHYSICAL_MEMORY_RANGE range = { 0 };
	PHYSICAL_ADDRESS highAddress = { 0 };
	LONGLONG sizeDifference = 0;
	__virtio16 numberOfBlocks = 0;
	PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
	PHYSICAL_ADDRESS skip = { 0 };
	PHYSICAL_ADDRESS address = { 0 };
	ULONG rangeCount = 0;

	address.QuadPart = (LONGLONG)Configuration->addr;

#if 0
	DumpSystemMemoryRanges(Configuration->addr,
		Configuration->addr + Configuration->plugged_size - 1);
#endif
	
	sizeDifference = (LONGLONG)(Configuration->plugged_size - Configuration->requested_size);

	//
	// Find the allocated block of memory that is less or equal to the size difference.
	//

	result = FindAllocatedMemoryRangeInBitmap(&devCtx->memoryBitmapHandle,
		address.QuadPart,
		Configuration->block_size,
		(ULONG)(sizeDifference / Configuration->block_size),
		&range);

	if (!result)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s FindAllocatedMemoryRangeInBitmap failed FALSE\n", __FUNCTION__);

		return FALSE;
	}

	//
	// Let's *try* to remove a memory range. 
	// 
	// Note: Removing the memory range may not be possible because the system can already use the 
	// whole range.
	// 

	//
	// The "skip" value is set to block_size, which means that the range address is aligned 
	// to this value.
	//

	skip.QuadPart = Configuration->block_size;

	//
	// The function MmAllocateNodePagesForMdlEx with the MM_ALLOCATE_AND_HOT_REMOVE flag must be 
	// used to remove the physical memory range from the system. While the name of this function 
	// is misleading, it is the only correct and documented way
	// (https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmallocatepagesformdlex)
	// 
	// Note that older DDKs also mention a function called MmRemovePhysicalMemory. However, the function 
	// is not documented and has limitations that exclude it from current usage.
	//

	ULONG flagsContigRemove = MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS
		| MM_ALLOCATE_AND_HOT_REMOVE;

	//
	// Trace information about the block to be removed.
	//

	LONGLONG startAddress = range.BaseAddress.QuadPart;
	LONGLONG endAddress = startAddress + (range.NumberOfBytes.QuadPart - 1);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, 
		"Range to be removed: start[0x%.8I64x] end[0x%.8I64x] size[0x%.8I64x]\n",
		startAddress,
		endAddress,
		range.NumberOfBytes.QuadPart);

	highAddress.QuadPart = range.BaseAddress.QuadPart + range.NumberOfBytes.QuadPart;

	//
	// Call the removal function - the mentioned MmAllocateNodePagesForMdlEx
	//
	
#if defined(_WIN64)

	LONGLONG bytesToRemoveCount = range.NumberOfBytes.QuadPart;

#else
	// 
	// Just a compilation fix for 32 bit platforms.
	//

	SIZE_T bytesToRemoveCount = (SIZE_T) range.NumberOfBytes.QuadPart;

#endif

	PMDL removedMemoryMdl = MmAllocateNodePagesForMdlEx(range.BaseAddress,
		highAddress,
		skip,
		bytesToRemoveCount,
		MmCached,
		0,
		flagsContigRemove
	);

	//
	// If the memory has been removed, convert MDLs returned by the MmAllocateNodePagesForMdlEx call to 
	// memory ranges, inform the device about removal, and then update the bitmap representation of 
    // memory blocks to reflect the change.
	//

	if (removedMemoryMdl)
	{

#if 0
		DumpSystemMemoryRanges(Configuration->addr,
			Configuration->addr + Configuration->plugged_size - 1);
#endif

		rangeCount = GetMemoryRangesFromMdl(removedMemoryMdl, devCtx->MemoryRange);

		for (ULONG i = 0; i < rangeCount; i++)
		{
			numberOfBlocks = (__virtio16)
				(devCtx->MemoryRange[i].NumberOfBytes.QuadPart / Configuration->block_size);
			
			if (SendUnPlugRequest(Device, devCtx->MemoryRange[i].BaseAddress.QuadPart, numberOfBlocks))
			{
				TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "HotRemove address [0x%I64x] len[0x%x]\n",
					devCtx->MemoryRange[i].BaseAddress.QuadPart,
					devCtx->MemoryRange[i].NumberOfBytes.QuadPart);

				DeallocateMemoryRangeInMemoryBitmap(address.QuadPart,
					&devCtx->MemoryRange[i],
					&devCtx->memoryBitmapHandle,
					(ULONG)Configuration->block_size);
			}
			else
			{
				//
				// If it was not possible to inform the device about range removal then 
				// revert the operation.
				//

				status = MmAddPhysicalMemory(&devCtx->MemoryRange[i].BaseAddress, 
					&devCtx->MemoryRange[i].NumberOfBytes);
				
				if (!NT_SUCCESS(status))
				{
					// 
					// It's not clear what to do if this operation fails?
					//

					TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
						"%s MmAddPhysicalMemory failed 0x%x\n", __FUNCTION__, status);

				}		
			}
		}
	}

	//
	// Return status OK.
	// Removal may fail because the system may already use the memory, but 
	// this situation(for obvious reasons) is not considered an error.
	//
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return TRUE\n", __FUNCTION__);

	return TRUE;
}


#if 0
//
// Function returns TRUE if a given memory range is on the list of system
// memory ranges. Otherwise it returns FALSE.
//
// Currently, the function is not being used. However, it is kept here for 
// reference if needed.
//

BOOLEAN IsMemoryRangeInUse(LONGLONG StartAddress, LONGLONG Size)
{
	PPHYSICAL_MEMORY_RANGE ranges;
	ULONG currentRange = 0;

	ranges = MmGetPhysicalMemoryRanges();

	if (ranges)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "start          end      size \n");
		for (currentRange = 0; ranges[currentRange].NumberOfBytes.QuadPart != 0; 
			currentRange++)
		{
			LONGLONG startAddress = ranges[currentRange].BaseAddress.QuadPart;
			LONGLONG endAddress = startAddress + (ranges[currentRange].NumberOfBytes.QuadPart - 1);

			if (startAddress >= StartAddress && endAddress <= Size)
			{
				//
				// Check if range is on the list of ranges allocated by the OS.
				//

				TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, 
					"Range 0x%.8I64x 0x%.8I64x 0x%.8I64x in USE!\n",
					startAddress,
					endAddress,
					ranges[currentRange].NumberOfBytes.QuadPart);

				ExFreePool(ranges);
				return TRUE;
			}
		}

		//
		// Free memory allocated for ranges structures.
		//

		ExFreePool(ranges);
	}
	else
	{
		//
		// It should never happen as there is ALWAYS at least 
		// range used by the system.
		//

		TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
			"MmGetPhysicalMemoryRanges returned 0 ranges!");
	}

	return FALSE;
}
#endif

//
// Function sends VIRTIO_MEM_REQ_STATE request to a device.
//	
//	Arguments: WdfDevice: device
//			   Address:			address of memory block to unplug
//			   NumberOfBlocks:	a number of contiguous blocks starting 
//                              at Address  
//								
//  Return value: TRUE if STATE operation finished with success and 
//						set state variable 
//				  FALSE if timeout occurred or device returned an error code
//

BOOLEAN SendStateRequest(
	IN WDFOBJECT WdfDevice,
	__virtio64 Address,
	__virtio16 NumberOfBlocks,
	__virtio16* state)
{
	PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
	VIO_SG              sg[2];
	BOOLEAN				do_notify = FALSE;
	NTSTATUS            status;
	PVOID buffer;
	UINT len;
	INT result = 0;

	//
	// Fill unplug request and response command buffers with zeros before submission.
	//

	memset(devCtx->MemoryResponse, 0, sizeof(virtio_mem_resp));
	memset(devCtx->plugRequest, 0, sizeof(virtio_mem_req));

	//
	// Build STATE request command.
	//

	devCtx->plugRequest->type = VIRTIO_MEM_REQ_STATE;
	devCtx->plugRequest->u.state.addr = Address;
	devCtx->plugRequest->u.state.nb_blocks = NumberOfBlocks;

	sg[0].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->plugRequest);
	sg[0].length = sizeof(virtio_mem_req);


	sg[1].physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice,
		devCtx->MemoryResponse);
	sg[1].length = sizeof(virtio_mem_resp);

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	result = virtqueue_add_buf(devCtx->infVirtQueue, sg, 1, 1, devCtx, NULL, 0);

	if (result < 0)
	{
		WdfSpinLockRelease(devCtx->infVirtQueueLock);

		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
			"%s: Cannot add buffer = [0x%x]\n", __FUNCTION__, result);
		return FALSE;
	}
	else
	{
		do_notify = virtqueue_kick_prepare(devCtx->infVirtQueue);
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

	if (do_notify)
	{
		virtqueue_notify(devCtx->infVirtQueue);
	}

	//
	// Wait indefinitely for the device's response.
	//

	status = KeWaitForSingleObject(
		&devCtx->hostAcknowledge,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s KeWaitForSingleObject failed!\n",
			__FUNCTION__);

		return FALSE;
	}

	WdfSpinLockAcquire(devCtx->infVirtQueueLock);
	if (virtqueue_has_buf(devCtx->infVirtQueue))
	{
		// 
		// Remove buffer from the virtio queue.
		//

		buffer = virtqueue_get_buf(devCtx->infVirtQueue, &len);
		if (buffer)
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
				"%s Buffer got, len = [%d]!\n", __FUNCTION__, len);
		}
		else
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
				"%s No buffer got, len = [%d]!\n", __FUNCTION__, len);

			WdfSpinLockRelease(devCtx->infVirtQueueLock);
			return FALSE;
		}
	}

	WdfSpinLockRelease(devCtx->infVirtQueueLock);

#if 0
	DumpViomemResponseType(devCtx->MemoryResponse);
#endif

	if (devCtx->MemoryResponse->type == VIRTIO_MEM_RESP_ACK)
	{
		*state = devCtx->MemoryResponse->u.state.state;
		TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
			"%s For address 0x%I64x, blocks count %hu get state %hu\n", __FUNCTION__,
			Address, NumberOfBlocks, devCtx->MemoryResponse->u.state.state);

		return TRUE;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
		"%s Failed to get state for address 0x%I64x, blocks count %hu\n", __FUNCTION__,
		Address, NumberOfBlocks);

	return FALSE;
}

//
// Function synchronizes host virtio-mem device and guest driver during 
// initialization.
// 
// Notes:
//		   1. According to virtio-spec, if after reset driver detects that memory
//		   is plugged (plugged_size > 0) the driver should: unplug memory or 
//         issue a STATE command. In this function STATE is issued.
//  
//		   2. According to section 5.15.6.2 of virtio spec[Device Requirements: 
//	       Device Operation], "the device should unplug all memory blocks 
//		   during system resets". So this code is for hypothetical scenario 
//		   because after reset plugged_size will be always set to zero.
//

BOOLEAN SynchronizeDeviceAndDriverMemory(IN WDFOBJECT Device, 
	virtio_mem_config *Config)
{
	PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
	BOOLEAN result = FALSE;
	if (devCtx)
	{
		//
		// Set all bit for unplugged state
		// bitmap will be filled with STATE requests
 		//
		RtlClearAllBits(&devCtx->memoryBitmapHandle);

		// region_size 
		//		is the size of device-managed memory region in bytes. Cannot change.
		// usable_region_size
		//	    is the size of the usable device-managed memory region. Can grow up to region_size.
		//	    Can only shrink due to VIRTIO_MEM_REQ_UNPLUG_ALL requests.
		// 
		// STATE request for memory > usable_region_size always FAIL

		ULONG NumberOfBlocks = (ULONG)(min(Config->region_size, Config->usable_region_size) / Config->block_size);

		PHYSICAL_ADDRESS address = { 0 };
		address.QuadPart = (LONGLONG)Config->addr;

		for (ULONG blockId = 0; blockId < NumberOfBlocks; blockId++)
		{
			__virtio16 state;
			__virtio64 startBlockAddr = Config->addr + blockId * Config->block_size;

			TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,
				"Processing block id=%d startBlockAddr=0x%I64x endBlockAddr=0x%I64x\n",
				blockId, startBlockAddr, startBlockAddr + Config->block_size);

			if (SendStateRequest(Device, startBlockAddr, 1, &state))
			{
				if (state == VIRTIO_MEM_STATE_PLUGGED)
				{
					PHYSICAL_MEMORY_RANGE range;

					range.BaseAddress.QuadPart = (LONGLONG)startBlockAddr;
					range.NumberOfBytes.QuadPart = (LONGLONG)Config->block_size;

					AllocateMemoryRangeInMemoryBitmap(address.QuadPart,
						&range,
						&devCtx->memoryBitmapHandle,
						(ULONG)Config->block_size);
				}
			}
			else
			{
				return FALSE;
			}
		}

		return TRUE;
	}

	return FALSE;
} 

//
// Main worker thread that processes init and virtio-mem configuration changes.
//

VOID ViomemWorkerThread(
	IN PVOID pContext
)
{
	NTSTATUS status = STATUS_SUCCESS;
	virtio_mem_config configReqest = { 0 };
	BOOLEAN result = FALSE;
	WDFOBJECT Device = (WDFOBJECT)pContext;
	PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "virtio-mem thread entry\n");

	for (;;)
	{
		status = KeWaitForSingleObject(&devCtx->WakeUpThread, Executive, KernelMode,
			FALSE, NULL);
		if (STATUS_WAIT_0 == status)
		{
			if (devCtx->finishProcessing)
			{
				// 
				// If shutdown requested finish processing loop and finish this thread.
				//
				
				break;
			}
			else
			{
				//
				// Read virtio-mem configuration and take action according to the configuration
				// changes: memory plug request or memory unplug request.
				//
				// Note: virtio-mem doesn't specify request type directly - it has to be deduced
				//	     based on memory configuration (requested size and plugged size fields).
				//

				VirtIOWdfDeviceGet(&devCtx->VDevice, 0, &configReqest, sizeof(configReqest));

				TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, 
					"Memory config: address [%I64x] requested_size [%I64x] plugged_size[%I64x] usable_region_size[%I64x]\n",
					configReqest.addr,
					configReqest.requested_size,
					configReqest.plugged_size,
					configReqest.usable_region_size);

				if (devCtx->state == VIOMEM_PROCESS_STATE_INIT)
				{
					// 
					// If memory is plugged then issue STATE requests.
					//

					if (configReqest.plugged_size > 0)
					{
						TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
							"Plugged memory detected during init, syncing...\n");

						result = SynchronizeDeviceAndDriverMemory(Device, &configReqest);
						if (result == FALSE)
						{
							//
							// Synchronization failed so quit this thread.
							//
							TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
								"Failed to SynchronizeDeviceAndDriverMemory...\n");
							break;
						}
					}

					// 
					// Change state to processing continue processing.
					//

					devCtx->state = VIOMEM_PROCESS_STATE_RUNNING;
				}

				//
				// The host device may send configuration update even if 
				// nothing changed (requested_size == plugged_size) so 
				// in this condition will be ignored.
				//

				if (configReqest.requested_size > configReqest.plugged_size)
				{
					//
					// Try to add memory to the system.
					//

					VirtioMemAddPhysicalMemory(Device, &configReqest);
				}
				else if (configReqest.requested_size < configReqest.plugged_size)
				{
					//
					// Try to remove memory from the system.
					//

					VirtioMemRemovePhysicalMemory(Device, &configReqest);
				}

			}
		}

	}
	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "virtio-mem thread exit\n");

	PsTerminateSystemThread(STATUS_SUCCESS);
}
