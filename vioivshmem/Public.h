#include <initguid.h>

DEFINE_GUID (GUID_DEVINTERFACE_VIOIVSHMEM,
    0xdf576976,0x569d,0x4672,0x95,0xa0,0xf5,0x7e,0x4e,0xa0,0xb2,0x10);
// {df576976-569d-4672-95a0-f57e4ea0b210}

typedef struct VIOIVSHMEM_MMAP
{
	size_t size; // the size of the memory region
	void * ptr;  // pointer to the memory region
}
VIOIVSHMEM_MMAP, *PVIOIVSHMEM_MMAP;

#define VIOIVSHMEM_IOCTL_REQUEST_SIZE 0
#define VIOIVSHMEM_IOCTL_REQUEST_MMAP 1
#define VIOIVSHMEM_IOCTL_RELEASE_MMAP 2