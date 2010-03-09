#ifndef VIOSERIAL_CORE_H
#define VIOSERIAL_CORE_H

// Core functions - prefixed with VSC (virtio-serial core)

NTSTATUS VSCInit(IN WDFOBJECT WdfDevice);
NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice);

NTSTATUS VSCGuestOpenedPort(/* TBD */);
void VSCGuestClosedPort(/* TBD */);

NTSTATUS VSCSendData(/* TBD ,*/PVOID pBuffer, size_t *pSize);
NTSTATUS VSCGetData(/* TBD ,*/WDFMEMORY *pMem, size_t *pSize);

#endif /* VIOSERIAL_CORE_H */
