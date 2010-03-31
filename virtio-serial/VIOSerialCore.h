#ifndef VIOSERIAL_CORE_H
#define VIOSERIAL_CORE_H

// Core functions - prefixed with VSC (virtio-serial core)

NTSTATUS VSCInit(IN WDFOBJECT WdfDevice);
NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice);

NTSTATUS VSCGuestOpenedPort(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext);
void VSCGuestClosedPort(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext);
void VSCGuestSetPortsReady(PDEVICE_CONTEXT pContext);

NTSTATUS VSCSendData(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContex, PVOID pBuffer, size_t *pSize);
NTSTATUS VSCGetData(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext, WDFMEMORY *pMem, size_t *pSize);

PVIOSERIAL_PORT MapFileToPort(WDFFILEOBJECT FileObject);
PDEVICE_CONTEXT GetContextFromFileObject(IN WDFFILEOBJECT FileObject);

void VIOSerialQueueRequest(IN PDEVICE_CONTEXT pContext,
						   IN WDFFILEOBJECT FileObject,
						   IN WDFREQUEST Request);


#endif /* VIOSERIAL_CORE_H */
