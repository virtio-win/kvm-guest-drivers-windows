#ifndef VIOSERIAL_H
#define VIOSERIAL_H

#define VIRTIO_SERIAL_MAX_PORTS 31
//#define VIRTIO_SERIAL_MAX_PORTS 31 //theoritical max ports value
// +1 for control queue
#define VIRTIO_SERIAL_MAX_QUEUES_COUPLES (VIRTIO_SERIAL_MAX_PORTS + 1)
#define VIRTIO_SERIAL_CONTROL_PORT_INDEX 1

#define VIRTIO_SERIAL_INVALID_INTERRUPT_STATUS 0xFF

//Feature list
#define VIRTIO_CONSOLE_F_MULTIPORT 1	/* Does host provide multiple ports? */

// Config struct
#pragma pack (push)
#pragma pack (1)

typedef struct virtio_console_config {
	//* colums of the screens
	u16 cols;
	//* rows of the screens
	u16 rows;
	//* max. number of ports this device can hold
	u32 max_nr_ports;
	//* number of ports added so far
	u32 nr_ports;
} VirtIOConsoleConfig, * PVirtIOConsoleConfig;
#pragma pack (pop)


typedef struct _tagCompletePhysicalAddress
{
	PHYSICAL_ADDRESS	Physical;
	PVOID				Virtual;
	//ULONG				IsCached		: 1;
	//ULONG				IsTX			: 1;
	// the size limit will be 1G instead of 4G
	//ULONG				size			: 30;
	ULONG				size;
} tCompletePhysicalAddress;

typedef struct _tagIODescriptor {
	LIST_ENTRY listEntry;
	tCompletePhysicalAddress DataInfo;
} IODescriptor, * pIODescriptor;

typedef struct _VIOSERIAL_PORT
{
	//LIST_ENTRY listEntry;
	unsigned int	NofReceiveBuffers;
	LIST_ENTRY		ReceiveBuffers;
	unsigned int	MaxReceiveBuffers;
	unsigned int	NofSendFreeBuffers;
	LIST_ENTRY		SendFreeBuffers;
	LIST_ENTRY		SendInUseBuffers;

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
