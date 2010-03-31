#ifndef VIOSERIAL_CORE_QUEUE_H
#define VIOSERIAL_CORE_QUEUE_H

#define VIRTIO_SERIAL_MAX_CONTROL_RECEIVE_BUFFERS 16
#define VIRTIO_SERIAL_MAX_PORT_RECEIVE_BUFFERS 128

void VSCCleanupQueues(IN PDEVICE_CONTEXT pContext);
NTSTATUS VSCInitQueues(IN PDEVICE_CONTEXT pContext);

NTSTATUS VSCSendCopyBuffer(PVIOSERIAL_PORT pPort,
						   PVOID buffer,
						   unsigned int size,
						   WDFSPINLOCK Lock,
						   BOOLEAN bKick);

NTSTATUS VSCRecieveCopyBuffer(PVIOSERIAL_PORT pPort,
							  WDFMEMORY * buffer,
							  size_t * pSize,
							  WDFSPINLOCK Lock,
							  BOOLEAN bDPC);

u32 VSCMapIndexToID(int index);
BOOLEAN AddRxBufferToQueue(PVIOSERIAL_PORT pPort, pIODescriptor pBufferDescriptor);

#endif /* VIOSERIAL_CORE_QUEUE_H */
