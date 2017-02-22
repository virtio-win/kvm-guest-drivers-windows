#ifndef PUBLIC_H
#define PUBLIC_H

#include <initguid.h>
// {E18B5FB3-04E4-42fc-9601-8395C217391B}
DEFINE_GUID(GUID_DEVINTERFACE_BALLOON,
0xe18b5fb3, 0x4e4, 0x42fc, 0x96, 0x1, 0x83, 0x95, 0xc2, 0x17, 0x39, 0x1b);

#define VIRTIO_BALLOON_S_SWAP_IN  0   /* Amount of memory swapped in */
#define VIRTIO_BALLOON_S_SWAP_OUT 1   /* Amount of memory swapped out */
#define VIRTIO_BALLOON_S_MAJFLT   2   /* Number of major faults */
#define VIRTIO_BALLOON_S_MINFLT   3   /* Number of minor faults */
#define VIRTIO_BALLOON_S_MEMFREE  4   /* Total amount of free memory */
#define VIRTIO_BALLOON_S_MEMTOT   5   /* Total amount of memory */
#define VIRTIO_BALLOON_S_AVAIL    6   /* Available memory */
#define VIRTIO_BALLOON_S_NR       7

#pragma pack (push)
#pragma pack (1)

typedef struct {
    USHORT tag;
    UINT64 val;
} BALLOON_STAT, *PBALLOON_STAT;
#pragma pack (pop)

#endif
