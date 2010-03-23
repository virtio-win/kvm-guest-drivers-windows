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

    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req          = (struct request *)Srb;
    srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_FLUSH;
    srbExt->out = 1;
    srbExt->in = 1;

    if (adaptExt->pci_vq_info.vq->vq_ops->add_buf(adaptExt->pci_vq_info.vq,
                     &srbExt->vbr.sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->vbr) >= 0){
        InsertTailList(&adaptExt->list_head, &srbExt->vbr.list_entry);
        adaptExt->pci_vq_info.vq->vq_ops->kick(adaptExt->pci_vq_info.vq);
        return TRUE;
    }

    RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s StorPortBusy\n", __FUNCTION__));
    StorPortBusy(DeviceExtension, 2);
    return FALSE;
}

BOOLEAN
RhelDoFlush(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedFlushRoutine, (PVOID)Srb);
}
#else
BOOLEAN
RhelDoFlush(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    int                 num_free;

    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req          = (struct request *)Srb;
    srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_FLUSH;
    srbExt->out = 1;
    srbExt->in = 1;

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

    RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s StorPortBusy\n", __FUNCTION__));
    StorPortBusy(DeviceExtension, 5);
    return FALSE;
}

BOOLEAN
RhelDoReadWrite(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    BOOLEAN res = FALSE;
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
#if (NTDDI_VERSION >= NTDDI_VISTA)
    NTSTATUS status = STATUS_SUCCESS;
    struct vring_desc *desc = NULL;
    srbExt->addr = NULL;
#endif
    res = StorPortSynchronizeAccess(DeviceExtension, SynchronizedReadWriteRoutine, (PVOID)Srb);
#if (NTDDI_VERSION >= NTDDI_VISTA)

    if (!res) {
        status = StorPortAllocatePool(DeviceExtension, 
                                     (srbExt->out + srbExt->in) * sizeof(struct vring_desc), 
                                     'rdnI', (PVOID)&desc);
        if (!NT_SUCCESS(status)) {
           RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s  StorPortAllocatePool failed 0x%x\n",  __FUNCTION__, status) );
           return FALSE;
        }
        srbExt->addr = desc;
        res = StorPortSynchronizeAccess(DeviceExtension, SynchronizedReadWriteRoutine, (PVOID)Srb);
    }
#endif
    return res;
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
        case SCSIOP_WRITE_VERIFY16: {
            REVERSE_BYTES_QUAD(&lba, &Cdb->CDB16.LogicalBlock[0]);
        }
        break;
        default: {
            ASSERT(FALSE);
            return (ULONGLONG)-1;
        }
    }

    return (lba.AsULongLong << adaptExt->info.physical_block_exp);
}
