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

#ifdef USE_STORPORT
BOOLEAN
SynchronizedAccessRoutine(
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
    StorPortBusy(DeviceExtension, 10);
    return FALSE;
}

BOOLEAN
RhelDoReadWrite(PVOID DeviceExtension,
                PSCSI_REQUEST_BLOCK Srb)
{
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedAccessRoutine, (PVOID)Srb);
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

    srbExt->vbr.out_hdr.sector = RhelGetLba(cdb);
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
