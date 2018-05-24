/*
 * This file contains various virtio queue related routines.
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
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
#include "virtio_stor_hw_helper.h"
#include"virtio_stor_utils.h"


#define SET_VA_PA() { ULONG len; va = adaptExt->indirect ? srbExt->desc : NULL; \
                      pa = va ? StorPortGetPhysicalAddress(DeviceExtension, NULL, va, &len).QuadPart : 0; \
                    }

BOOLEAN
RhelDoFlush(
    PVOID DeviceExtension,
    PSRB_TYPE Srb,
    BOOLEAN resend,
    BOOLEAN bIsr
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    ULONG               fragLen = 0UL;
    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;

    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    bool                notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    struct virtqueue    *vq = NULL;

    SET_VA_PA();

    if (resend) {
        MessageId = srbExt->MessageID;
        QueueNumber = MessageId - 1;
    } else if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%s srb %p, cpu %d :: QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n",  __FUNCTION__, Srb, srbExt->cpu, QueueNumber, param.MessageNumber, param.ChannelNumber));
        }
        else {
           RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s StorPortGetStartIoPerfParams failed. srb %p cpu %d status 0x%x.\n",__FUNCTION__, Srb, srbExt->cpu, status));
           QueueNumber = 0;
           MessageId = 1;
        }
    } else {
        QueueNumber = 0;
        MessageId = 1;
    }

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = VIRTIO_BLK_T_FLUSH;
    srbExt->out                = 1;
    srbExt->in                 = 1;

    srbExt->sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->sg[0].length   = sizeof(srbExt->vbr.out_hdr);
    srbExt->sg[1].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->sg[1].length   = sizeof(srbExt->vbr.status);

    VioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        result = TRUE;
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
    }
    else {
        VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s Can not add packet to queue %d.\n", __FUNCTION__, QueueNumber));
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        virtqueue_notify(vq);
    }

    return result;
}

BOOLEAN
RhelDoReadWrite(PVOID DeviceExtension,
                PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    PVOID               va = NULL;
    ULONGLONG           pa = 0ULL;

    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    bool                notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    struct virtqueue    *vq = NULL;

    SET_VA_PA();

    if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
           MessageId = param.MessageNumber;
           QueueNumber = MessageId - 1;
           RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%s srb %p, cpu %d :: QueueNumber %lu, MessageNumber %lu, ChannelNumber %lu.\n", __FUNCTION__, Srb, srbExt->cpu, QueueNumber, param.MessageNumber, param.ChannelNumber));
        }
        else {
           RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s StorPortGetStartIoPerfParams failed srb %p cpu %d status 0x%x.\n", __FUNCTION__, Srb, srbExt->cpu, status));
           QueueNumber = 0;
           MessageId = 1;
        }
    }
    else {
        QueueNumber = 0;
        MessageId = 1;
    }

    srbExt->MessageID = MessageId;
    vq = adaptExt->vq[QueueNumber];
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("<--->%s : QueueNumber 0x%x vq = %p\n", __FUNCTION__, QueueNumber, vq));

    VioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr, va, pa) >= 0) {
        notify = virtqueue_kick_prepare(vq);
        VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
        result = TRUE;
    }
    else {
        VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s Can not add packet to queue %d.\n", __FUNCTION__, QueueNumber));
        StorPortBusy(DeviceExtension, 2);
    }
    if (notify) {
        virtqueue_notify(vq);
    }

#if (NTDDI_VERSION > NTDDI_WIN7)
    if (adaptExt->num_queues > 1) {
        if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
           VioStorCompleteRequest(DeviceExtension, MessageId, FALSE);
        }
    }
#endif
    return result;
}

VOID
RhelShutDown(
    IN PVOID DeviceExtension
    )
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    virtio_device_reset(&adaptExt->vdev);
    virtio_delete_queues(&adaptExt->vdev);
    for (index = 0; index < adaptExt->num_queues; ++index) {
        adaptExt->vq[index] = NULL;
    }
    virtio_device_shutdown(&adaptExt->vdev);
}

ULONGLONG
RhelGetLba(
    IN PVOID DeviceExtension,
    IN PCDB Cdb
    )
{

    EIGHT_BYTE lba;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    lba.AsULongLong = 0;

    switch (Cdb->CDB6GENERIC.OperationCode) {

        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_WRITE_VERIFY: {
            lba.Byte0 = Cdb->CDB10.LogicalBlockByte3;
            lba.Byte1 = Cdb->CDB10.LogicalBlockByte2;
            lba.Byte2 = Cdb->CDB10.LogicalBlockByte1;
            lba.Byte3 = Cdb->CDB10.LogicalBlockByte0;
        }
        break;
        case SCSIOP_READ6:
        case SCSIOP_WRITE6: {
            lba.Byte0 = Cdb->CDB6READWRITE.LogicalBlockMsb1;
            lba.Byte1 = Cdb->CDB6READWRITE.LogicalBlockMsb0;
            lba.Byte2 = Cdb->CDB6READWRITE.LogicalBlockLsb;
        }
        break;
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE_VERIFY12: {
            REVERSE_BYTES(&lba, &Cdb->CDB12.LogicalBlock[0]);
        }
        break;
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_WRITE_VERIFY16: {
            REVERSE_BYTES_QUAD(&lba, &Cdb->CDB16.LogicalBlock[0]);
        }
        break;
        default: {
            ASSERT(FALSE);
            return (ULONGLONG)-1;
        }
    }
    return (lba.AsULongLong * (adaptExt->info.blk_size / SECTOR_SIZE));
}

VOID
RhelGetSerialNumber(
    IN PVOID DeviceExtension
)
{
    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    struct virtqueue    *vq = NULL;
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    VIO_SG              sg[3];

    QueueNumber = 0;
    MessageId = 1;
    vq = adaptExt->vq[QueueNumber];

    adaptExt->vbr.out_hdr.type = VIRTIO_BLK_T_GET_ID | VIRTIO_BLK_T_IN;
    adaptExt->vbr.out_hdr.sector = 0;
    adaptExt->vbr.out_hdr.ioprio = 0;

    sg[0].physAddr = MmGetPhysicalAddress(&adaptExt->vbr.out_hdr);
    sg[0].length   = sizeof(adaptExt->vbr.out_hdr);
    sg[1].physAddr = MmGetPhysicalAddress(&adaptExt->sn);
    sg[1].length   = sizeof(adaptExt->sn);
    sg[2].physAddr = MmGetPhysicalAddress(&adaptExt->vbr.status);
    sg[2].length   = sizeof(adaptExt->vbr.status);

//    VioStorVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(vq,
                     &sg[0],
                     1, 2,
                     &adaptExt->vbr, NULL, 0) >= 0) {
#ifdef DBG
        InterlockedIncrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
        virtqueue_kick_always(vq);
    }
//    VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
}

VOID
RhelGetDiskGeometry(
    IN PVOID DeviceExtension
)
{
    u64                cap;
    u32                v;
    struct virtio_blk_geometry vgeo;

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    adaptExt->features = virtio_get_features(&adaptExt->vdev);

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BARRIER\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_RO\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, size_max),
                          &v, sizeof(v));
        adaptExt->info.size_max = v;
    } else {
        adaptExt->info.size_max = SECTOR_SIZE;
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, seg_max),
                          &v, sizeof(v));
        adaptExt->info.seg_max = v;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_SEG_MAX = %d\n", adaptExt->info.seg_max));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, blk_size),
                          &v, sizeof(v));
        adaptExt->info.blk_size = v;
    } else {
        adaptExt->info.blk_size = SECTOR_SIZE;
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BLK_SIZE = %d\n", adaptExt->info.blk_size));

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, geometry),
                          &vgeo, sizeof(vgeo));
        adaptExt->info.geometry.cylinders= vgeo.cylinders;
        adaptExt->info.geometry.heads    = vgeo.heads;
        adaptExt->info.geometry.sectors  = vgeo.sectors;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_GEOMETRY. cylinders = %d  heads = %d  sectors = %d\n", adaptExt->info.geometry.cylinders, adaptExt->info.geometry.heads, adaptExt->info.geometry.sectors));
    }

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, capacity),
                      &cap, sizeof(cap));
    adaptExt->info.capacity = cap;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("capacity = %08I64X\n", adaptExt->info.capacity));


    if(CHECKBIT(adaptExt->features, VIRTIO_BLK_F_TOPOLOGY)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, physical_block_exp),
                          &adaptExt->info.physical_block_exp, sizeof(adaptExt->info.physical_block_exp));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("physical_block_exp = %d\n", adaptExt->info.physical_block_exp));

        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, alignment_offset),
                          &adaptExt->info.alignment_offset, sizeof(adaptExt->info.alignment_offset));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("alignment_offset = %d\n", adaptExt->info.alignment_offset));

        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, min_io_size),
                          &adaptExt->info.min_io_size, sizeof(adaptExt->info.min_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("min_io_size = %d\n", adaptExt->info.min_io_size));

        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, opt_io_size),
                          &adaptExt->info.opt_io_size, sizeof(adaptExt->info.opt_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("opt_io_size = %d\n", adaptExt->info.opt_io_size));
    }
}

VOID
VioStorVQLock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN OUT PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
    PADAPTER_EXTENSION  adaptExt;
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("--->%s MessageID = %d\n", __FUNCTION__, MessageID));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    if (!isr) {
        if (adaptExt->msix_enabled) {
            if (adaptExt->num_queues > 1) {

                NT_ASSERT(MessageID > 0);
                NT_ASSERT(MessageID <= adaptExt->num_queues);
                StorPortAcquireSpinLock(DeviceExtension, DpcLock, &adaptExt->dpc[MessageID - 1], LockHandle);
            }
            else {
                ULONG oldIrql = 0;
                StorPortAcquireMSISpinLock(DeviceExtension, (adaptExt->msix_one_vector ? 0 : MessageID), &oldIrql);
                LockHandle->Context.OldIrql = (KIRQL)oldIrql;
            }
        }
        else {
            StorPortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, LockHandle);
        }
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("<---%s MessageID = %d\n", __FUNCTION__, MessageID));
}

VOID
VioStorVQUnlock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
    PADAPTER_EXTENSION  adaptExt;
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("--->%s MessageID = %d\n", __FUNCTION__, MessageID));
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!isr) {
        if (adaptExt->num_queues > 1) {
            StorPortReleaseSpinLock(DeviceExtension, LockHandle);
        }
        else {
            StorPortReleaseMSISpinLock(DeviceExtension, (adaptExt->msix_one_vector ? 0 : MessageID), LockHandle->Context.OldIrql);
        }
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("<---%s MessageID = %d\n", __FUNCTION__, MessageID));
}
