#include <Windows.h>
#include "Hardware.h"
#include "..\testcommands.h"
#include "HardwareTypes.h"
#include "..\IONetDescriptor.h"

#define virtio_net_hdr _tagvirtio_net_hdr
#define virtio_net_hdr_mrg_rxbuf _tagvirtio_net_hdr_ext

#define VIRTIO_PCI_VRING_ALIGN         4096

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT       1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT  4

/* This means don't notify other side when buffer added. */
#define VRING_USED_F_NO_NOTIFY  1
/* This means don't interrupt guest when buffer consumed. */
#define VRING_AVAIL_F_NO_INTERRUPT      1

#define VIRTQUEUE_MAX_SIZE 1024

#define trace_virtqueue_fill(...)
#define trace_virtqueue_flush(...)
#define trace_virtqueue_pop(...)


struct iovec
{
    char *iov_base;
    uint32_t iov_len;
};

static size_t qemu_sendv_packet_async(PVOID p, struct iovec *out_sg, unsigned int out_num, void *completeproc);


typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    target_phys_addr_t in_addr[VIRTQUEUE_MAX_SIZE];
    target_phys_addr_t out_addr[VIRTQUEUE_MAX_SIZE];
    struct iovec in_sg[VIRTQUEUE_MAX_SIZE];
    struct iovec out_sg[VIRTQUEUE_MAX_SIZE];
} VirtQueueElement;


typedef struct VRingDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VRingDesc;

typedef struct VRingAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[0];
} VRingAvail;

typedef struct VRingUsedElem
{
    uint32_t id;
    uint32_t len;
} VRingUsedElem;

typedef struct VRingUsed
{
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[0];
} VRingUsed;

typedef struct VRing
{
    unsigned int num;
    target_phys_addr_t desc;
    target_phys_addr_t avail;
    target_phys_addr_t used;
} VRing;

typedef struct VirtQueue
{
    VRing vring;
    target_phys_addr_t pa;
    uint16_t last_avail_idx;
    /* Last used index value we have signalled on */
    uint16_t signalled_used;

    /* Last used index value we have signalled on */
    bool signalled_used_valid;

    /* Notification enabled? */
    bool notification;

    int inuse;

    uint16_t vector;
    uint16_t interruptCount;
    void (*handle_output)(PVOID hardwareDevice, struct VirtQueue *vq);
    PVOID hardwareDevice;
}VirtQueue;

static int32_t virtio_net_flush_tx(PVOID hardwareDevice, VirtQueue *vq);


static inline target_phys_addr_t vring_align(target_phys_addr_t addr,
                                             unsigned long align)
{
    return (addr + align - 1) & ~(align - 1);
}



/* virt queue functions */
static void virtqueue_init(VirtQueue *vq)
{
    target_phys_addr_t pa = vq->pa;

    vq->vring.desc = pa;
    vq->vring.avail = pa + vq->vring.num * sizeof(VRingDesc);
    vq->vring.used = vring_align(vq->vring.avail +
                                 offsetof(VRingAvail, ring[vq->vring.num]),
                                 VIRTIO_PCI_VRING_ALIGN);
}

static void virtqueue_reset(VirtQueue *vq)
{
    vq->vring.desc = vq->vring.avail = vq->vring.used = 0;
    vq->pa = 0;
    vq->last_avail_idx = 0;
    vq->inuse = 0;
    vq->notification = 0;
    vq->signalled_used = 0;
    vq->signalled_used_valid = 0;
}

static inline uint64_t vring_desc_addr(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, addr);
    return ldq_phys(pa);
}

static inline uint32_t vring_desc_len(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, len);
    return ldl_phys(pa);
}

static inline uint16_t vring_desc_flags(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, flags);
    return lduw_phys(pa);
}

static inline uint16_t vring_desc_next(target_phys_addr_t desc_pa, int i)
{
    target_phys_addr_t pa;
    pa = desc_pa + sizeof(VRingDesc) * i + offsetof(VRingDesc, next);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_flags(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, flags);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_idx(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, idx);
    return lduw_phys(pa);
}

static inline uint16_t vring_avail_ring(VirtQueue *vq, int i)
{
    target_phys_addr_t pa;
    pa = vq->vring.avail + offsetof(VRingAvail, ring[i]);
    return lduw_phys(pa);
}

static inline uint16_t vring_used_event(VirtQueue *vq)
{
    return vring_avail_ring(vq, vq->vring.num);
}

static inline void vring_used_ring_id(VirtQueue *vq, int i, uint32_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].id);
    stl_phys(pa, val);
}

static inline void vring_used_ring_len(VirtQueue *vq, int i, uint32_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, ring[i].len);
    stl_phys(pa, val);
}

static uint16_t vring_used_idx(VirtQueue *vq)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    return lduw_phys(pa);
}

static inline void vring_used_idx_set(VirtQueue *vq, uint16_t val)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, idx);
    stw_phys(pa, val);
}

static inline void vring_used_flags_set_bit(VirtQueue *vq, int mask)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(pa, lduw_phys(pa) | mask);
}

static inline void vring_used_flags_unset_bit(VirtQueue *vq, int mask)
{
    target_phys_addr_t pa;
    pa = vq->vring.used + offsetof(VRingUsed, flags);
    stw_phys(pa, lduw_phys(pa) & ~mask);
}

static inline void vring_avail_event(VirtQueue *vq, uint16_t val)
{
    target_phys_addr_t pa;
    if (!vq->notification) {
        return;
    }
    pa = vq->vring.used + offsetof(VRingUsed, ring[vq->vring.num]);
    stw_phys(pa, val);
}

/* Assuming a given event_idx value from the other size, if
 * we have just incremented index from old to new_idx,
 * should we trigger an event? */
static inline int vring_need_event(uint16_t _event, uint16_t _new, uint16_t old)
{
    /* Note: Xen has similar logic for notification hold-off
     * in include/xen/interface/io/ring.h with req_event and req_prod
     * corresponding to event_idx + 1 and new respectively.
     * Note also that req_event and req_prod in Xen start at 1,
     * event indexes in virtio start at 0. */
    return (uint16_t)(_new - _event - 1) < (uint16_t)(_new - old);
}

static bool vring_notify(PVOID unused, VirtQueue *vq)
{
    uint16_t old, _new;
    bool v;
    /* Always notify when queue is empty (when feature acknowledge) */
    //if (((vdev->guest_features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY)) &&
    if ((bVirtioF_NotifyOnEmpty &&
         !vq->inuse && vring_avail_idx(vq) == vq->last_avail_idx)) {
        return TRUE;
    }

    //if (!(vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX))) {
    if (!bUsePublishedIndices) {
        return !(vring_avail_flags(vq) & VRING_AVAIL_F_NO_INTERRUPT);
    }

    v = vq->signalled_used_valid;
    vq->signalled_used_valid = TRUE;
    old = vq->signalled_used;
    _new = vq->signalled_used = vring_used_idx(vq);
    return !v || vring_need_event(vring_used_event(vq), _new, old);
}


void virtio_queue_set_notification(VirtQueue *vq, int enable)
{
    vq->notification = enable;
    if (bUsePublishedIndices) {
        vring_avail_event(vq, vring_avail_idx(vq));
    } else if (enable) {
        vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
    } else {
        vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);
    }
}

int virtio_queue_ready(VirtQueue *vq)
{
    return vq->vring.avail != 0;
}

int virtio_queue_empty(VirtQueue *vq)
{
    return vring_avail_idx(vq) == vq->last_avail_idx;
}

void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx)
{
    unsigned int offset;
    unsigned int i;

    trace_virtqueue_fill(vq, elem, len, idx);

    offset = 0;
    for (i = 0; i < elem->in_num; i++) {
        size_t size = MIN(len - offset, elem->in_sg[i].iov_len);

        cpu_physical_memory_unmap(elem->in_sg[i].iov_base,
                                  elem->in_sg[i].iov_len,
                                  1, size);

        offset += elem->in_sg[i].iov_len;
    }

    for (i = 0; i < elem->out_num; i++)
        cpu_physical_memory_unmap(elem->out_sg[i].iov_base,
                                  elem->out_sg[i].iov_len,
                                  0, elem->out_sg[i].iov_len);

    idx = (idx + vring_used_idx(vq)) % vq->vring.num;

    /* Get a pointer to the next entry in the used ring. */
    vring_used_ring_id(vq, idx, elem->index);
    vring_used_ring_len(vq, idx, len);
}

void virtqueue_flush(VirtQueue *vq, unsigned int count)
{
    uint16_t old, new;
    /* Make sure buffer is written before we update index. */
    wmb();
    trace_virtqueue_flush(vq, count);
    old = vring_used_idx(vq);
    new = old + count;
    vring_used_idx_set(vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old)))
        vq->signalled_used_valid = false;
}

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len)
{
    virtqueue_fill(vq, elem, len, 0);
    virtqueue_flush(vq, 1);
}

static int virtqueue_num_heads(VirtQueue *vq, unsigned int idx)
{
    uint16_t num_heads = vring_avail_idx(vq) - idx;

    /* Check it isn't doing very strange things with descriptor numbers. */
    if (num_heads > vq->vring.num) {
        error_report("Guest moved used index from %u to %u",
                     idx, vring_avail_idx(vq));
        exit(1);
    }

    return num_heads;
}

static unsigned int virtqueue_get_head(VirtQueue *vq, unsigned int idx)
{
    unsigned int head;

    /* Grab the next descriptor number they're advertising, and increment
     * the index we've seen. */
    head = vring_avail_ring(vq, idx % vq->vring.num);

    /* If their number is silly, that's a fatal mistake. */
    if (head >= vq->vring.num) {
        error_report("Guest says index %u is available", head);
        exit(1);
    }

    return head;
}

static unsigned virtqueue_next_desc(target_phys_addr_t desc_pa,
                                    unsigned int i, unsigned int max)
{
    unsigned int next;

    /* If this descriptor says it doesn't chain, we're done. */
    if (!(vring_desc_flags(desc_pa, i) & VRING_DESC_F_NEXT))
        return max;

    /* Check they're not leading us off end of descriptors. */
    next = vring_desc_next(desc_pa, i);
    /* Make sure compiler knows to grab that: we don't want it changing! */
    wmb();

    if (next >= max) {
        error_report("Desc next is %u", next);
        exit(1);
    }

    return next;
}

int virtqueue_avail_bytes(VirtQueue *vq, int in_bytes, int out_bytes)
{
    unsigned int idx;
    int total_bufs, in_total, out_total;

    idx = vq->last_avail_idx;

    total_bufs = in_total = out_total = 0;
    while (virtqueue_num_heads(vq, idx)) {
        unsigned int max, num_bufs, indirect = 0;
        target_phys_addr_t desc_pa;
        int i;

        max = vq->vring.num;
        num_bufs = total_bufs;
        i = virtqueue_get_head(vq, idx++);
        desc_pa = vq->vring.desc;

        if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
            if (vring_desc_len(desc_pa, i) % sizeof(VRingDesc)) {
                error_report("Invalid size for indirect buffer table");
                exit(1);
            }

            /* If we've got too many, that implies a descriptor loop. */
            if (num_bufs >= max) {
                error_report("Looped descriptor");
                exit(1);
            }

            /* loop over the indirect descriptor table */
            indirect = 1;
            max = vring_desc_len(desc_pa, i) / sizeof(VRingDesc);
            num_bufs = i = 0;
            desc_pa = vring_desc_addr(desc_pa, i);
        }

        do {
            /* If we've got too many, that implies a descriptor loop. */
            if (++num_bufs > max) {
                error_report("Looped descriptor");
                exit(1);
            }

            if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
                if (in_bytes > 0 &&
                    (in_total += vring_desc_len(desc_pa, i)) >= in_bytes)
                    return 1;
            } else {
                if (out_bytes > 0 &&
                    (out_total += vring_desc_len(desc_pa, i)) >= out_bytes)
                    return 1;
            }
        } while ((i = virtqueue_next_desc(desc_pa, i, max)) != max);

        if (!indirect)
            total_bufs = num_bufs;
        else
            total_bufs++;
    }

    return 0;
}

void virtqueue_map_sg(struct iovec *sg, target_phys_addr_t *addr,
    size_t num_sg, int is_write)
{
    unsigned int i;
    target_phys_addr_t len;

    for (i = 0; i < num_sg; i++) {
        len = sg[i].iov_len;
        sg[i].iov_base = cpu_physical_memory_map(addr[i], &len, is_write);
        if (sg[i].iov_base == NULL || len != sg[i].iov_len) {
            error_report("virtio: trying to map MMIO memory");
            exit(1);
        }
    }
}

int virtqueue_pop(VirtQueue *vq, VirtQueueElement *elem)
{
    unsigned int i, head, max;
    target_phys_addr_t desc_pa = vq->vring.desc;

    if (!virtqueue_num_heads(vq, vq->last_avail_idx))
        return 0;

    /* When we start there are none of either input nor output. */
    elem->out_num = elem->in_num = 0;

    max = vq->vring.num;

    i = head = virtqueue_get_head(vq, vq->last_avail_idx++);
    if (bUsePublishedIndices) {
        vring_avail_event(vq, vring_avail_idx(vq));
    }

    if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
        if (vring_desc_len(desc_pa, i) % sizeof(VRingDesc)) {
            error_report("Invalid size for indirect buffer table");
            exit(1);
        }

        /* loop over the indirect descriptor table */
        max = vring_desc_len(desc_pa, i) / sizeof(VRingDesc);
        desc_pa = vring_desc_addr(desc_pa, i);
        i = 0;
    }

    /* Collect all the descriptors */
    do {
        struct iovec *sg;

        if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
            if (elem->in_num >= ARRAY_SIZE(elem->in_sg)) {
                error_report("Too many write descriptors in indirect table");
                exit(1);
            }
            elem->in_addr[elem->in_num] = vring_desc_addr(desc_pa, i);
            sg = &elem->in_sg[elem->in_num++];
        } else {
            if (elem->out_num >= ARRAY_SIZE(elem->out_sg)) {
                error_report("Too many read descriptors in indirect table");
                exit(1);
            }
            elem->out_addr[elem->out_num] = vring_desc_addr(desc_pa, i);
            sg = &elem->out_sg[elem->out_num++];
        }

        sg->iov_len = vring_desc_len(desc_pa, i);

        /* If we've got too many, that implies a descriptor loop. */
        if ((elem->in_num + elem->out_num) > max) {
            error_report("Looped descriptor");
            exit(1);
        }
    } while ((i = virtqueue_next_desc(desc_pa, i, max)) != max);

    /* Now map what we have collected */
    virtqueue_map_sg(elem->in_sg, elem->in_addr, elem->in_num, 1);
    virtqueue_map_sg(elem->out_sg, elem->out_addr, elem->out_num, 0);

    elem->index = head;

    vq->inuse++;

    trace_virtqueue_pop(vq, elem, elem->in_num, elem->out_num);
    return elem->in_num + elem->out_num;
}

static void virtio_queue_notify_vq(VirtQueue *vq)
{
    if (vq->vring.desc)
    {
        //VirtIODevice *vdev = vq->vdev;
        PVOID hardwareDevice = vq->hardwareDevice;
        //trace_virtio_queue_notify(hardwareDevice, vq);
        vq->handle_output(hardwareDevice, vq);
    }
}

#define MAC_TABLE_ENTRIES    64
#define MAX_VLAN    (1 << 12)   /* Per 802.1Q definition */

typedef struct _tag_filtering
{
    uint8_t promisc;
    uint8_t allmulti;
    uint8_t alluni;
    uint8_t nomulti;
    uint8_t nouni;
    uint8_t nobcast;
    struct {
        int in_use;
        int first_multi;
        uint8_t multi_overflow;
        uint8_t uni_overflow;
        uint8_t macs[MAC_TABLE_ENTRIES * ETH_ALEN];
    } mac_table;
    uint32_t vlans[MAX_VLAN >> 5];
} tFiltering;

typedef struct _tHardwareDevice
{
    PVOID hostDev;

    VirtQueue tx;
    VirtQueue rx;
    VirtQueue ctrl;
    VirtQueue aux;

    BYTE status;
    BYTE interrupt;

    unsigned long TxInterrupts;
    unsigned long RxInterrupts;

    BOOLEAN bShallComplete;

    struct
    {
        VirtQueueElement elem;
        unsigned int len;
    } async_tx;

    tFiltering filtering;

}tHardwareDevice;

static void reset_filtering(tFiltering *n)
{
    /* Reset back to compatibility mode */
    n->promisc = 1;
    n->allmulti = 0;
    n->alluni = 0;
    n->nomulti = 0;
    n->nouni = 0;
    n->nobcast = 0;

    /* Flush any MAC and VLAN filter table state */
    n->mac_table.in_use = 0;
    n->mac_table.first_multi = 0;
    n->mac_table.multi_overflow = 0;
    n->mac_table.uni_overflow = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);
    memset(n->vlans, 0, MAX_VLAN >> 3);
}


static void resetDevice(tHardwareDevice *pd)
{
    virtqueue_reset(&pd->rx);
    virtqueue_reset(&pd->tx);
    virtqueue_reset(&pd->ctrl);
    virtqueue_reset(&pd->aux);

    reset_filtering(&pd->filtering);
}

void virtio_notify(tHardwareDevice *pd, VirtQueue *pQueue)
{
    if (!vring_notify(NULL, pQueue)) {
        return;
    }
    pQueue->interruptCount++;
    pd->interrupt |= pQueue->vector;
}

static void virtio_net_handle_rx(PVOID hardwareDevice, VirtQueue *vq)
{

}

static void virtio_net_tx_complete(PVOID hardwareDevice, size_t len)
{
//    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;
    tHardwareDevice *pd = (tHardwareDevice *)hardwareDevice;

    virtqueue_push(&pd->tx, &pd->async_tx.elem, pd->async_tx.len);
    virtio_notify(pd, &pd->tx);

    pd->async_tx.elem.out_num = pd->async_tx.len = 0;

    virtio_queue_set_notification(&pd->tx, 1);
    virtio_net_flush_tx(pd, &pd->tx);
}


/* TX */
static int32_t virtio_net_flush_tx(PVOID hardwareDevice, VirtQueue *vq)
{
    tHardwareDevice *pd = (tHardwareDevice *)hardwareDevice;
    VirtQueueElement elem;
    int32_t num_packets = 0;

    if (pd->async_tx.elem.out_num) {
        virtio_queue_set_notification(&pd->tx, 0);
        return num_packets;
    }

    while (virtqueue_pop(vq, &elem)) {
        size_t ret, len = 0;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];
        unsigned hdr_len;

        /* hdr_len refers to the header received from the guest */
        hdr_len = bUseMergedBuffers ?
            sizeof(struct virtio_net_hdr_mrg_rxbuf) :
            sizeof(struct virtio_net_hdr);

        if (out_num < 1 || out_sg->iov_len != hdr_len) {
            error_report("virtio-net header not in first element");
            exit(1);
        }

        /* ignore the header if GSO is not supported */
        if (!bHostHasVnetHdr) {
            out_num--;
            out_sg++;
            len += hdr_len;
        } else if (bUseMergedBuffers) {
            /* tapfd expects a struct virtio_net_hdr */
            hdr_len -= sizeof(struct virtio_net_hdr);
            out_sg->iov_len -= hdr_len;
            len += hdr_len;
        }

        ret = qemu_sendv_packet_async(pd, out_sg, out_num,
                                      virtio_net_tx_complete);
        if (ret == 0) {
            virtio_queue_set_notification(&pd->tx, 0);
            pd->async_tx.elem = elem;
            pd->async_tx.len  = len;
            return -2;
        }

        len += ret;

        virtqueue_push(vq, &elem, len);
        virtio_notify(pd, vq);

#if 0
        if (++num_packets >= n->tx_burst) {
            break;
#else
        { ++num_packets;
#endif
        }
    }
    return num_packets;
}

static int virtio_net_handle_rx_mode(tFiltering *n, uint8_t cmd,
                                     VirtQueueElement *elem)
{
    uint8_t on;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(on)) {
        error_report("virtio-net ctrl invalid rx mode command");
        exit(1);
    }

    on = ldub_p(elem->out_sg[1].iov_base);

    if (cmd == VIRTIO_NET_CTRL_RX_MODE_PROMISC)
        n->promisc = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_ALLMULTI)
        n->allmulti = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_ALLUNI)
        n->alluni = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOMULTI)
        n->nomulti = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOUNI)
        n->nouni = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOBCAST)
        n->nobcast = on;
    else
        return VIRTIO_NET_ERR;

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_mac(tFiltering *n, uint8_t cmd,
                                 VirtQueueElement *elem)
{
    struct virtio_net_ctrl_mac mac_data;

    if (cmd != VIRTIO_NET_CTRL_MAC_TABLE_SET || elem->out_num != 3 ||
        elem->out_sg[1].iov_len < sizeof(mac_data) ||
        elem->out_sg[2].iov_len < sizeof(mac_data))
        return VIRTIO_NET_ERR;

    n->mac_table.in_use = 0;
    n->mac_table.first_multi = 0;
    n->mac_table.uni_overflow = 0;
    n->mac_table.multi_overflow = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);

    mac_data.entries = ldl_p(elem->out_sg[1].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[1].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries <= MAC_TABLE_ENTRIES) {
        memcpy(n->mac_table.macs, elem->out_sg[1].iov_base + sizeof(mac_data),
               mac_data.entries * ETH_ALEN);
        n->mac_table.in_use += mac_data.entries;
    } else {
        n->mac_table.uni_overflow = 1;
    }

    n->mac_table.first_multi = n->mac_table.in_use;

    mac_data.entries = ldl_p(elem->out_sg[2].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[2].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries) {
        if (n->mac_table.in_use + mac_data.entries <= MAC_TABLE_ENTRIES) {
            memcpy(n->mac_table.macs + (n->mac_table.in_use * ETH_ALEN),
                   elem->out_sg[2].iov_base + sizeof(mac_data),
                   mac_data.entries * ETH_ALEN);
            n->mac_table.in_use += mac_data.entries;
        } else {
            n->mac_table.multi_overflow = 1;
        }
    }

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_vlan_table(tFiltering *n, uint8_t cmd,
                                        VirtQueueElement *elem)
{
    uint16_t vid;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(vid)) {
        error_report("virtio-net ctrl invalid vlan command");
        return VIRTIO_NET_ERR;
    }

    vid = lduw_p(elem->out_sg[1].iov_base);

    if (vid >= MAX_VLAN)
        return VIRTIO_NET_ERR;

    if (cmd == VIRTIO_NET_CTRL_VLAN_ADD)
        n->vlans[vid >> 5] |= (1U << (vid & 0x1f));
    else if (cmd == VIRTIO_NET_CTRL_VLAN_DEL)
        n->vlans[vid >> 5] &= ~(1U << (vid & 0x1f));
    else
        return VIRTIO_NET_ERR;

    return VIRTIO_NET_OK;
}

static void LogTestFlowMac(const tFiltering *f, const char *header, int from, int to)
{
    if (from < to)
    {
        int i;
        const uint8_t *base = f->mac_table.macs;
        LogTestFlow("%s:\n", header);
        for (i = from; i < to; ++i)
        {
            const uint8_t *p = base + i * ETH_ALEN;
            LogTestFlow("[%d]=%02X%02X%02X%02X%02X%02X\n", i, *p, *(p+1), *(p+2), *(p+3), *(p+4), *(p+5));
        }
    }
}

static void PrintFiltersState(const tFiltering *f)
{
    int i;
    LogTestFlow("Promisc=%d\n",f->promisc);
    LogTestFlow("AllMulti=%d\n",f->allmulti);
    LogTestFlow("AllUni=%d\n",f->alluni);
    LogTestFlow("NoMulti=%d\n",f->nomulti);
    LogTestFlow("NoUni=%d\n",f->nouni);
    LogTestFlow("NoBroad=%d\n",f->nobcast);
    if (f->mac_table.multi_overflow)
        LogTestFlow("Mac table multicast overflow\n");
    if (f->mac_table.uni_overflow)
        LogTestFlow("Mac table unicast overflow\n");
    LogTestFlowMac(f, "Unicast table", 0, f->mac_table.first_multi);
    LogTestFlowMac(f, "Multicast table", f->mac_table.first_multi, f->mac_table.in_use);
    for (i = 0; i < MAX_VLAN; ++i)
    {
        if (f->vlans[i >> 5] & (1 << (i & 0x1f)))
            LogTestFlow("VLAN 0x%X enabled\n", i);
    }
}


static void virtio_net_handle_ctrl(PVOID hardwareDevice, VirtQueue *vq)
{
    tHardwareDevice *pd = (tHardwareDevice *)hardwareDevice;
    virtio_net_ctrl_hdr ctrl;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    VirtQueueElement elem;

    while (virtqueue_pop(vq, &elem)) {
        if ((elem.in_num < 1) || (elem.out_num < 1)) {
            error_report("virtio-net ctrl missing headers");
            exit(1);
        }

        if (elem.out_sg[0].iov_len < sizeof(ctrl) ||
            elem.in_sg[elem.in_num - 1].iov_len < sizeof(status)) {
            error_report("virtio-net ctrl header not in correct element");
            exit(1);
        }

        ctrl.class = ldub_p(elem.out_sg[0].iov_base);
        ctrl.cmd = ldub_p(elem.out_sg[0].iov_base + sizeof(ctrl.class));

        if (ctrl.class == VIRTIO_NET_CTRL_RX_MODE)
            status = virtio_net_handle_rx_mode(&pd->filtering, ctrl.cmd, &elem);
        else if (ctrl.class == VIRTIO_NET_CTRL_MAC)
            status = virtio_net_handle_mac(&pd->filtering, ctrl.cmd, &elem);
        else if (ctrl.class == VIRTIO_NET_CTRL_VLAN)
            status = virtio_net_handle_vlan_table(&pd->filtering, ctrl.cmd, &elem);

        stb_p(elem.in_sg[elem.in_num - 1].iov_base, status);

        virtqueue_push(vq, &elem, sizeof(status));
        virtio_notify(pd, vq);
    }

    PrintFiltersState(&pd->filtering);
}


/*
static void virtio_handle_ctrl(PVOID hardwareDevice, VirtQueue *vq)
{
    tHardwareDevice *pd = (tHardwareDevice *)hardwareDevice;
    VirtQueueElement elem;
    while (virtqueue_pop(vq, &elem)) {
        size_t ret, len = 0;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];

        ret = 1;
        len += ret;

        virtqueue_push(vq, &elem, len);
        virtio_notify(pd, vq);
    }
    LogTestFlow("%s\n",__FUNCTION__);
}
*/

static void virtio_handle_aux(PVOID hardwareDevice, VirtQueue *vq)
{
    tHardwareDevice *pd = (tHardwareDevice *)hardwareDevice;
    VirtQueueElement elem;
    while (virtqueue_pop(vq, &elem)) {
        size_t ret, len = 0;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];

        ret = 1;
        len += ret;

        virtqueue_push(vq, &elem, len);
        virtio_notify(pd, vq);
    }
    LogTestFlow("%s\n", __FUNCTION__);
}


static void virtio_net_handle_tx_timer(PVOID hardwareDevice, VirtQueue *vq)
{
    virtio_queue_set_notification(vq, 1);
#if 0
    qemu_del_timer(n->tx_timer);
    n->tx_waiting = 0;
#endif
    virtio_net_flush_tx(hardwareDevice, vq);
}

// from iov.c
size_t iov_from_buf(struct iovec *iov, unsigned int iov_cnt,
                    const void *buf, size_t iov_off, size_t size)
{
    size_t iovec_off, buf_off;
    unsigned int i;

    iovec_off = 0;
    buf_off = 0;
    for (i = 0; i < iov_cnt && size; i++) {
        if (iov_off < (iovec_off + iov[i].iov_len)) {
            size_t len = MIN((iovec_off + iov[i].iov_len) - iov_off, size);

            memcpy(iov[i].iov_base + (iov_off - iovec_off), (const char *)buf + buf_off, len);

            buf_off += len;
            iov_off += len;
            size -= len;
        }
        iovec_off += iov[i].iov_len;
    }
    return buf_off;
}

size_t iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                  void *buf, size_t iov_off, size_t size)
{
    uint8_t *ptr;
    size_t iovec_off, buf_off;
    unsigned int i;

    ptr = buf;
    iovec_off = 0;
    buf_off = 0;
    for (i = 0; i < iov_cnt && size; i++) {
        if (iov_off < (iovec_off + iov[i].iov_len)) {
            size_t len = MIN((iovec_off + iov[i].iov_len) - iov_off , size);

            memcpy(ptr + buf_off, iov[i].iov_base + (iov_off - iovec_off), len);

            buf_off += len;
            iov_off += len;
            size -= len;
        }
        iovec_off += iov[i].iov_len;
    }
    return buf_off;
}

size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}
// end of from iov.c



void *hwCreateDevice(void *pHostDevice)
{
    tHardwareDevice *pd = (tHardwareDevice *)malloc(sizeof(tHardwareDevice));
    memset(pd, 0, sizeof(*pd));
    pd->hostDev = pHostDevice;
    pd->rx.vring.num = 256;
    pd->rx.vector = RXQ_INTERRUPT_VECTOR;
    pd->rx.handle_output = virtio_net_handle_rx;
    pd->rx.hardwareDevice = pd;

    pd->tx.vring.num = 256;
    pd->tx.vector = TXQ_INTERRUPT_VECTOR;
    pd->tx.handle_output = virtio_net_handle_tx_timer;
    pd->tx.hardwareDevice = pd;

    pd->ctrl.vring.num = 4;
    pd->ctrl.vector = CTL_INTERRUPT_VECTOR;
    pd->ctrl.handle_output = virtio_net_handle_ctrl;
    pd->ctrl.hardwareDevice = pd;

    pd->aux.vring.num = 16;
    pd->aux.vector = AUX_INTERRUPT_VECTOR;
    pd->aux.handle_output = virtio_handle_aux;
    pd->aux.hardwareDevice = pd;

    resetDevice(pd);

    return pd;
}

void hwDestroyDevice(void *pHw)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    free(pd);
}

unsigned short hwGetQueueSize(void *pHw, unsigned short queueIndex)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    USHORT val = 0;
    switch(queueIndex)
    {
        case RX_QUEUE_NUMBER:
            val = pd->rx.vring.num;
            break;
        case TX_QUEUE_NUMBER:
            val = pd->tx.vring.num;
            break;
        case CTL_QUEUE_NUMBER:
            val = pd->ctrl.vring.num;
            break;
        case AUX_QUEUE_NUMBER:
            val = pd->aux.vring.num;
            break;
        default:
            break;
    }
    return val;
}

ULONG hwGetQueuePfn(void *pHw, unsigned short queueIndex)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    ULONG val = 0;
    switch(queueIndex)
    {
        case RX_QUEUE_NUMBER:
            val = (ULONG)(pd->rx.pa >> 12);
            break;
        case TX_QUEUE_NUMBER:
            val = (ULONG)(pd->tx.pa >> 12);
            break;
        case CTL_QUEUE_NUMBER:
            val = (ULONG)(pd->ctrl.pa >> 12);
            break;
        case AUX_QUEUE_NUMBER:
            val = (ULONG)(pd->aux.pa >> 12);
            break;
        default:
            FailCase("[%s](%d)", __FUNCTION__, queueIndex);
            break;
    }
    return val;
}

void hwSetQueuePfn(void *pHw, unsigned short queueIndex, ULONG val)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    switch(queueIndex)
    {
        case RX_QUEUE_NUMBER:
            pd->rx.pa = ((ULONGLONG)val) << 12;
            virtqueue_init(&pd->rx);
            break;
        case TX_QUEUE_NUMBER:
            pd->tx.pa = ((ULONGLONG)val) << 12;
            virtqueue_init(&pd->tx);
            break;
        case CTL_QUEUE_NUMBER:
            pd->ctrl.pa = ((ULONGLONG)val) << 12;
            virtqueue_init(&pd->ctrl);
            break;
        case AUX_QUEUE_NUMBER:
            pd->aux.pa = ((ULONGLONG)val) << 12;
            virtqueue_init(&pd->aux);
            break;
        default:
            FailCase("[%s](%d)", __FUNCTION__, queueIndex);
            break;
    }
}

BYTE hwReadInterruptStatus(void *pHw)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    BYTE b = pd->interrupt;
    pd->interrupt = 0;
    return b;
}

BYTE hwGetDeviceStatus(void *pHw)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    return pd->status;
}


void hwSetDeviceStatus(void *pHw, BYTE val)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    pd->status = val;
    if (!val)
    {
        resetDevice(pd);
    }
}

void hwQueueNotify(void *pHw, WORD wValue)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    switch(wValue)
    {
        case RX_QUEUE_NUMBER:
            virtio_queue_notify_vq(&pd->rx);
            break;
        case TX_QUEUE_NUMBER:
            virtio_queue_notify_vq(&pd->tx);
            break;
        case CTL_QUEUE_NUMBER:
            virtio_queue_notify_vq(&pd->ctrl);
            break;
        case AUX_QUEUE_NUMBER:
            virtio_queue_notify_vq(&pd->aux);
            break;
        default:
            FailCase("[%s](%d)", __FUNCTION__, wValue);
            break;
    }
}

static int virtio_net_can_receive(tHardwareDevice *pd)
{
    return virtio_queue_ready(&pd->rx);
}

static int virtio_net_has_buffers(tHardwareDevice *pd, int bufsize)
{
    if (virtio_queue_empty(&pd->rx) ||
        (bUseMergedBuffers &&
         !virtqueue_avail_bytes(&pd->rx, bufsize, 0))) {
        virtio_queue_set_notification(&pd->rx, 1);

        /* To avoid a race condition where the guest has made some buffers
         * available after the above check but before notification was
         * enabled, check for available buffers again.
         */
        if (virtio_queue_empty(&pd->rx) ||
            (bUseMergedBuffers &&
             !virtqueue_avail_bytes(&pd->rx, bufsize, 0)))
            return 0;
    }

    virtio_queue_set_notification(&pd->rx, 0);
    return 1;
}

#define work_around_broken_dhclient(...)

static int receive_header(tHardwareDevice *pd, struct iovec *iov, int iovcnt,
                          const void *buf, size_t size, size_t hdr_len)
{
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)iov[0].iov_base;
    int offset = 0;

    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;

    if (bHostHasVnetHdr) {
        memcpy(hdr, buf, sizeof(*hdr));
        offset = sizeof(*hdr);
        work_around_broken_dhclient(hdr, buf + offset, size - offset);
    }

    /* We only ever receive a struct virtio_net_hdr from the tapfd,
     * but we may be passing along a larger header to the guest.
     */
    iov[0].iov_base += hdr_len;
    iov[0].iov_len  -= hdr_len;

    return offset;
}



#define receive_filter(...) 1

static size_t virtio_net_receive(void *pHw, const uint8_t *buf, size_t size)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    struct virtio_net_hdr_mrg_rxbuf *mhdr = NULL;
    size_t guest_hdr_len, offset, i, host_hdr_len;

    if (!virtio_net_can_receive(pd))
        return -1;

    /* hdr_len refers to the header we supply to the guest */
    guest_hdr_len = bUseMergedBuffers ?
        sizeof(struct virtio_net_hdr_mrg_rxbuf) : sizeof(struct virtio_net_hdr);


    host_hdr_len = bHostHasVnetHdr ? sizeof(struct virtio_net_hdr) : 0;
    if (!virtio_net_has_buffers(pd, size + guest_hdr_len - host_hdr_len))
        return 0;

    if (!receive_filter(n, buf, size))
        return size;

    offset = i = 0;

    while (offset < size) {
        VirtQueueElement elem;
        int len, total;
        struct iovec sg[VIRTQUEUE_MAX_SIZE];

        total = 0;

        if (virtqueue_pop(&pd->rx, &elem) == 0) {
            if (i == 0)
                return -1;
            error_report("virtio-net unexpected empty queue: "
                    "i %zd mergeable %d offset %zd, size %zd, "
                    "guest hdr len %zd, host hdr len %zd guest features 0x%x",
                    i, bUseMergedBuffers, offset, size,
                    guest_hdr_len, host_hdr_len, 0xdeaddead);
            exit(1);
        }

        if (elem.in_num < 1) {
            error_report("virtio-net receive queue contains no in buffers");
            exit(1);
        }

        if (!bUseMergedBuffers && elem.in_sg[0].iov_len != guest_hdr_len) {
            error_report("virtio-net header not in first element");
            exit(1);
        }

        memcpy(&sg, &elem.in_sg[0], sizeof(sg[0]) * elem.in_num);

        if (i == 0) {
            if (bUseMergedBuffers)
                mhdr = (struct virtio_net_hdr_mrg_rxbuf *)sg[0].iov_base;

            offset += receive_header(pd, sg, elem.in_num,
                                     buf + offset, size - offset, guest_hdr_len);
            total += guest_hdr_len;
        }

        /* copy in packet.  ugh */
        len = iov_from_buf(sg, elem.in_num,
                           buf + offset, 0, size - offset);
        total += len;
        offset += len;
        /* If buffers can't be merged, at this point we
         * must have consumed the complete packet.
         * Otherwise, drop it. */
        if (!bUseMergedBuffers && offset < size) {
#if 0
            error_report("virtio-net truncated non-mergeable packet: "
                         "i %zd mergeable %d offset %zd, size %zd, "
                         "guest hdr len %zd, host hdr len %zd",
                         i, n->mergeable_rx_bufs,
                         offset, size, guest_hdr_len, host_hdr_len);
#endif
            return size;
        }

        /* signal other side */
        virtqueue_fill(&pd->rx, &elem, total, i++);
    }

    if (mhdr) {
        stw_p(&mhdr->nBuffers, i);
    }

    virtqueue_flush(&pd->rx, i);
    virtio_notify(pd, &pd->rx);

    return size;
}

size_t qemu_sendv_packet_async(PVOID p, struct iovec *out_sg, unsigned int out_num, void *completeproc)
{
    tHardwareDevice *pd = (tHardwareDevice *)p;
    unsigned int i;
    for (i = 0; i < out_num; ++i)
    {
        LogTestFlow("[%s] sending entry %p, len %d\n", __FUNCTION__, out_sg[i].iov_base, out_sg[i].iov_len);
    }
    if (!bAsyncTransmit)
    {
        pd->bShallComplete = FALSE;
    }
    else
    {
        LogTestFlow("[%s] %d entries pending\n", __FUNCTION__, out_num);
        pd->bShallComplete = TRUE;
    }
    return !bAsyncTransmit;
}

void hwCheckInterrupt(void *pHw)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    if (pd->rx.interruptCount)
        LogTestFlow(" Raised %d RX interrupt%s\n", pd->rx.interruptCount, pd->rx.interruptCount == 1 ? "" : "s");
    if (pd->tx.interruptCount)
        LogTestFlow(" Raised %d TX interrupt%s\n", pd->tx.interruptCount, pd->tx.interruptCount == 1 ? "" : "s");
    pd->RxInterrupts += pd->rx.interruptCount;
    pd->TxInterrupts += pd->tx.interruptCount;
    pd->rx.interruptCount = pd->tx.interruptCount = 0;
}

void hwReceiveBuffer(void *pHw, void *buffer, size_t size)
{
    size_t sizeReceived = virtio_net_receive(pHw, (uint8_t *)buffer, size);
    hwCheckInterrupt(pHw);
    if (sizeReceived != size)
    {
        FailCase("[%s] %d != %d", __FUNCTION__, size, sizeReceived);
    }
}

void hwCompleteTx(void *pHw, int num)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    // anyway, the len parameter is ignored
    while (num-- && pd->bShallComplete)
    {
        pd->bShallComplete = FALSE;
        virtio_net_tx_complete(pHw, 0);
    }
    hwCheckInterrupt(pHw);
}

void hwGetInterrups(void *pHw, unsigned long *ptx, unsigned long *prx)
{
    tHardwareDevice *pd = (tHardwareDevice *)pHw;
    *ptx = pd->TxInterrupts;
    *prx = pd->RxInterrupts;
}

static BYTE DeviceData[] = { 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89 };

bool hwReadDeviceData(void *pHw, ULONG reg, BYTE *pval)
{
    ULONG lowerLimit, upperLimit;
    bool b;
    lowerLimit = bMSIXUsed ? 24 : 20;
    upperLimit = 32;
    b = reg >= lowerLimit && reg < upperLimit;
    if (b) *pval = DeviceData[reg - lowerLimit];
    return b;
}

bool hwWriteDeviceData(void *pHw, ULONG reg, BYTE val)
{
    ULONG lowerLimit, upperLimit;
    bool b;
    lowerLimit = bMSIXUsed ? 24 : 20;
    upperLimit = 32;
    b = reg >= lowerLimit && reg < upperLimit;
    if (b) DeviceData[reg - lowerLimit] = val;
    return b;
}

