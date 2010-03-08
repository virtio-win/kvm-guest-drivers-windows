#ifndef VIOSERIAL_H
#define VIOSERIAL_H

#define VIRTIO_SERIAL_MAX_PORTS 1
// +1 for control queue
#define VIRTIO_SERIAL_MAX_QUEUES (VIRTIO_SERIAL_MAX_PORTS + 1)

typedef struct _VIOSERIAL_DEVICE
{
	//LIST_ENTRY listEntry;
	unsigned int	NofReceiveBuffers;
	LIST_ENTRY		ReceiveBuffers;
	unsigned int	NofSendBuffers;
	LIST_ENTRY		SendBuffers;

	struct virtqueue *		ReceiveQueues;
	struct virtqueue *		SendQueues;

	// The 'id' to identify the port with the Host
	u32 id;

	// Is the host device open
	bool bHostConnected;

	// We should allow only one process to open a port
	bool bGuestConnected;
} VIOSERIAL_DEVICE, *PVIOSERIAL_DEVICE;


#endif /* VIOSERIAL_H */
