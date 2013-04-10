#ifndef hw_h
#define hw_h

#include <windows.h>

#define inline __inline

typedef ULONGLONG target_phys_addr_t;

struct iovec
{
    void *iov_base;  /* BSD uses caddr_t (1003.1g requires void *) */
    size_t iov_len; /* Must be size_t (1003.1g) */
};

#define cpu_physical_memory_unmap(...)
#define cpu_physical_memory_map(addr, plen, is_write)   addr

uint64_t inline ldq_phys(target_phys_addr_t pa)
{
    return *(uint64_t *)pa;
}

uint32_t inline ldl_phys(target_phys_addr_t pa)
{
    return *(uint32_t *)pa;
}

uint16_t inline lduw_phys(target_phys_addr_t pa)
{
    return *(uint16_t *)pa;
}

void inline stl_phys(target_phys_addr_t pa, uint32_t val)
{
    *(uint32_t *)pa = val;
}

void inline stw_phys(target_phys_addr_t pa, uint16_t val)
{
    *(uint16_t *)pa = val;
}


#endif
