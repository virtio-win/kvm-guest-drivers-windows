#ifndef VIOSERIAL_CORE_CONTROL
#define VIOSERIAL_CORE_CONTROL

#pragma pack (push)
#pragma pack (1)
typedef struct _virtio_console_control {
	u32 id;		/* Port number */
	u16 event;		/* The kind of control event (see below) */
	u16 value;		/* Extra information for the key */
}VIRTIO_CONSOLE_CONTROL, * PVIRTIO_CONSOLE_CONTROL;
#pragma pack (pop)

/* Some events for control messages */
#define VIRTIO_CONSOLE_PORT_READY	0
#define VIRTIO_CONSOLE_CONSOLE_PORT	1
#define VIRTIO_CONSOLE_RESIZE		2
#define VIRTIO_CONSOLE_PORT_OPEN	3
#define VIRTIO_CONSOLE_PORT_NAME	4
#define VIRTIO_CONSOLE_PORT_REMOVE	5

NTSTATUS SendControlMessage(PDEVICE_CONTEXT pContext,
							u32 uiPortID, // Control for this port
							u16 uiEvent, // Event 
							u16 uiValue); //Event related data

#endif /* VIOSERIAL_CORE_CONTROL */
