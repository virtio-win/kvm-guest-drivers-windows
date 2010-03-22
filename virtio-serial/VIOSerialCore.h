#ifndef VIOSERIAL_CORE_H
#define VIOSERIAL_CORE_H

// Core functions - prefixed with VSC (virtio-serial core)

NTSTATUS VSCInit(IN WDFOBJECT WdfDevice);
NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice);

NTSTATUS VSCGuestOpenedPort(PDEVICE_CONTEXT pContext);
void VSCGuestClosedPort(PDEVICE_CONTEXT pContext);
void VSCGuestSetPortsReady(PDEVICE_CONTEXT pContext);

NTSTATUS VSCSendData(PDEVICE_CONTEXT pContex, PVOID pBuffer, size_t *pSize);
NTSTATUS VSCGetData(PDEVICE_CONTEXT pContex, WDFMEMORY *pMem, size_t *pSize);

#endif /* VIOSERIAL_CORE_H */
