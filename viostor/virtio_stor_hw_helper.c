/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: virtio_stor_hw_helper.c
 *
 * Author(s):
 * Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains various virtio queue related routines.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "virtio_stor_hw_helper.h"
#include"virtio_stor_utils.h"

#ifdef USE_STORPORT
BOOLEAN
SynchronizedFlushRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK) Context;
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    ULONG               fragLen;

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = VIRTIO_BLK_T_FLUSH;
    srbExt->out                = 1;
    srbExt->in                 = 1;

    srbExt->vbr.sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->vbr.sg[0].ulSize   = sizeof(srbExt->vbr.out_hdr);
    srbExt->vbr.sg[1].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->vbr.sg[1].ulSize   = sizeof(srbExt->vbr.status);

    if (adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                     &srbExt->vbr.sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr) >= 0) {
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
        return TRUE;
    }
    return FALSE;
}

ULONG
RhelDoFlush(
    PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG i;
    ULONG Wait = 10000;

    ASSERT(adaptExt->flush_done != TRUE);
    if(StorPortSynchronizeAccess(DeviceExtension, SynchronizedFlushRoutine, (PVOID)Srb)) {
        for (i = 0; i < Wait; i++) {
           if (adaptExt->flush_done == TRUE) {
              adaptExt->flush_done = FALSE;
              return Srb->SrbStatus;
           }
           StorPortStallExecution(500);
           if (adaptExt->dump_mode) {
              VirtIoInterrupt(DeviceExtension);
           }
        }
    }
    return SRB_STATUS_ERROR;
}
#else
ULONG
RhelDoFlush(
    PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    ULONG               fragLen;
    int                 num_free;
    ULONG               i;
    ULONG               Wait   = 10000;
    ULONG               status = SRB_STATUS_ERROR;

    srbExt->vbr.out_hdr.sector = 0;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;
    srbExt->vbr.out_hdr.type   = VIRTIO_BLK_T_FLUSH;
    srbExt->out                = 1;
    srbExt->in                 = 1;

    srbExt->vbr.sg[0].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->vbr.sg[0].ulSize   = sizeof(srbExt->vbr.out_hdr);
    srbExt->vbr.sg[1].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->vbr.sg[1].ulSize   = sizeof(srbExt->vbr.status);

    num_free = adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                                      &srbExt->vbr.sg[0],
                                      srbExt->out, srbExt->in,
                                      &srbExt->vbr);
    if ( num_free >= 0) {
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
        for (i = 0; i < Wait; i++) {
           if (adaptExt->flush_done == TRUE) {
              adaptExt->flush_done = FALSE;
              status = Srb->SrbStatus;
              break;
           }
           ScsiPortStallExecution(500);
           VirtIoInterrupt(DeviceExtension);
        }
    }
    if (status != SRB_STATUS_SUCCESS) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s  0x%x\n",  __FUNCTION__, status) );
        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);
        status = SRB_STATUS_SUCCESS;
    }
    ScsiPortNotification(NextLuRequest, DeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
    return status;
}
#endif

#ifdef USE_STORPORT
BOOLEAN
SynchronizedReadWriteRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK) Context;
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;

    if (adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                     &srbExt->vbr.sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr) >= 0){
        InsertTailList(&adaptExt->list_head, &srbExt->vbr.list_entry);
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
        return TRUE;
    }
    StorPortBusy(DeviceExtension, 5);
    return FALSE;
}

BOOLEAN
RhelDoReadWrite(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedReadWriteRoutine, (PVOID)Srb);
}
#else
BOOLEAN
RhelDoReadWrite(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    PCDB                  cdb;
    ULONG                 fragLen;
    ULONG                 sgElement;
    ULONG                 BytesLeft;
    PVOID                 DataBuffer;
    PADAPTER_EXTENSION    adaptExt;
    PRHEL_SRB_EXTENSION   srbExt;
    int                   num_free;

    cdb      = (PCDB)&Srb->Cdb[0];
    srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BytesLeft= Srb->DataTransferLength;
    DataBuffer = Srb->DataBuffer;
    sgElement = 1;

    while (BytesLeft) {
        srbExt->vbr.sg[sgElement].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, Srb, DataBuffer, &fragLen);
        srbExt->vbr.sg[sgElement].ulSize = fragLen;
        BytesLeft -= fragLen;
        DataBuffer = (PVOID)((ULONG_PTR)DataBuffer + fragLen);
        sgElement++;
    }

    srbExt->vbr.out_hdr.sector = RhelGetLba(DeviceExtension, cdb);
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;

    if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_OUT;
        srbExt->out = sgElement;
        srbExt->in = 1;
    } else {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_IN;
        srbExt->out = 1;
        srbExt->in = sgElement;
    }

    srbExt->vbr.sg[0].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &fragLen);
    srbExt->vbr.sg[0].ulSize = sizeof(srbExt->vbr.out_hdr);

    srbExt->vbr.sg[sgElement].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &fragLen);
    srbExt->vbr.sg[sgElement].ulSize = sizeof(srbExt->vbr.status);
    num_free = adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                                      &srbExt->vbr.sg[0],
                                      srbExt->out, srbExt->in,
                                      &srbExt->vbr);

    if ( num_free >= 0) {
        InsertTailList(&adaptExt->list_head, &srbExt->vbr.list_entry);
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
        srbExt->call_next = FALSE;
        if(num_free < VIRTIO_MAX_SG) {
           srbExt->call_next = TRUE;
        } else {
           ScsiPortNotification(NextLuRequest, DeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun);
        }
    }
    return TRUE;
}
#endif

VOID
RhelShutDown(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    VirtIODeviceReset(DeviceExtension);
    ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_GUEST_FEATURES), 0);
    if (adaptExt->pci_vq_info.vq) {
       adaptExt->pci_vq_info.vq->vq_ops->shutdown(adaptExt->pci_vq_info.vq);
       VirtIODeviceDeleteVirtualQueue(adaptExt->pci_vq_info.vq);
       adaptExt->pci_vq_info.vq = NULL;
    }
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
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->vbr.out_hdr.type = VIRTIO_BLK_T_GET_ID | VIRTIO_BLK_T_IN;
    adaptExt->vbr.out_hdr.sector = 0;
    adaptExt->vbr.out_hdr.ioprio = 0;

    adaptExt->vbr.sg[0].physAddr = MmGetPhysicalAddress(&adaptExt->vbr.out_hdr);
    adaptExt->vbr.sg[0].ulSize   = sizeof(adaptExt->vbr.out_hdr);
    adaptExt->vbr.sg[1].physAddr = MmGetPhysicalAddress(&adaptExt->sn);
    adaptExt->vbr.sg[1].ulSize   = sizeof(adaptExt->sn);
    adaptExt->vbr.sg[2].physAddr = MmGetPhysicalAddress(&adaptExt->vbr.status);
    adaptExt->vbr.sg[2].ulSize   = sizeof(adaptExt->vbr.status);

    if (adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                     &adaptExt->vbr.sg[0],
                     1, 2,
                     &adaptExt->vbr) >= 0) {
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
    }
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


    adaptExt->features = ScsiPortReadPortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));


    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BARRIER\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_RO\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, size_max),
                      &v, sizeof(v));
        adaptExt->info.size_max = v;
    } else {
        adaptExt->info.size_max = SECTOR_SIZE;
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, seg_max),
                      &v, sizeof(v));
        adaptExt->info.seg_max = v;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_SEG_MAX = %d\n", adaptExt->info.seg_max));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, blk_size),
                      &v, sizeof(v));
        adaptExt->info.blk_size = v;
    } else {
        adaptExt->info.blk_size = SECTOR_SIZE;
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BLK_SIZE = %d\n", adaptExt->info.blk_size));

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, geometry),
                      &vgeo, sizeof(vgeo));
        adaptExt->info.geometry.cylinders= vgeo.cylinders;
        adaptExt->info.geometry.heads    = vgeo.heads;
        adaptExt->info.geometry.sectors  = vgeo.sectors;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_GEOMETRY. cylinders = %d  heads = %d  sectors = %d\n", adaptExt->info.geometry.cylinders, adaptExt->info.geometry.heads, adaptExt->info.geometry.sectors));
    }

    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, capacity),
                      &cap, sizeof(cap));
    adaptExt->info.capacity = cap;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("capacity = %08I64X\n", adaptExt->info.capacity));


    if(CHECKBIT(adaptExt->features, VIRTIO_BLK_F_TOPOLOGY)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, physical_block_exp),
                      &adaptExt->info.physical_block_exp, sizeof(adaptExt->info.physical_block_exp));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("physical_block_exp = %d\n", adaptExt->info.physical_block_exp));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, alignment_offset),
                      &adaptExt->info.alignment_offset, sizeof(adaptExt->info.alignment_offset));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("alignment_offset = %d\n", adaptExt->info.alignment_offset));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, min_io_size),
                      &adaptExt->info.min_io_size, sizeof(adaptExt->info.min_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("min_io_size = %d\n", adaptExt->info.min_io_size));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, opt_io_size),
                      &adaptExt->info.opt_io_size, sizeof(adaptExt->info.opt_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("opt_io_size = %d\n", adaptExt->info.opt_io_size));
    }
}

