#ifndef VIOSERIAL_H
#define VIOSERIAL_H

#define VIRTIO_SERIAL_MAX_PORTS 1
//#define VIRTIO_SERIAL_MAX_PORTS 31 //theoritical max ports value
// +1 for control queue
#define VIRTIO_SERIAL_MAX_QUEUES_COUPLES (VIRTIO_SERIAL_MAX_PORTS + 1)

typedef struct _VIOSERIAL_PORT
{
	//LIST_ENTRY listEntry;
	unsigned int	NofReceiveBuffers;
	LIST_ENTRY		ReceiveBuffers;
	unsigned int	NofSendBuffers;
	LIST_ENTRY		SendBuffers;

	struct virtqueue *		ReceiveQueue;
	struct virtqueue *		SendQueue;

	// The 'id' to identify the port with the Host
	u32 id;

	// Is the host device open
	bool bHostConnected;

	// We should allow only one process to open a port
	bool bGuestConnected;
} VIOSERIAL_PORT, *PVIOSERIAL_PORT;


#endif /* VIOSERIAL_H */
