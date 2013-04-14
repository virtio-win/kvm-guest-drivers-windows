#ifndef hardwaretypes_h
#define hardwaretypes_h

typedef unsigned short uint16_t, u16;
typedef short int16_t;
typedef unsigned char uint8_t, u8;
typedef unsigned long uint32_t;
typedef unsigned __int64 uint64_t;
typedef long int32_t;

typedef uint64_t target_phys_addr_t;

#define offsetof(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
#define inline __inline
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define unlikely(x) (x)
#define false FALSE
#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])

static inline uint64_t ldq_phys(target_phys_addr_t pa)
{
    return *(uint64_t *)pa;
}

static inline uint32_t ldl_phys(target_phys_addr_t pa)
{
    return *(uint32_t *)pa;
}

static inline uint16_t lduw_phys(target_phys_addr_t pa)
{
    return *(uint16_t *)pa;
}

static inline void stl_phys(target_phys_addr_t pa, uint32_t val)
{
    *(uint32_t *)pa = val;
}

static inline void stw_phys(target_phys_addr_t pa, uint16_t val)
{
    *(uint16_t *)pa = val;
}

static inline void stw_p(uint16_t *va, uint16_t val)
{
    *va = val;
}

static inline void stb_p(uint8_t *va, uint8_t val)
{
    *va = val;
}

static inline uint8_t ldub_p(uint8_t *va)
{
    return *va;
}

static inline uint32_t ldl_p(void *va)
{
    return *(uint32_t *)va;
}

static inline uint16_t lduw_p(void *va)
{
    return *(uint16_t *)va;
}

#define cpu_physical_memory_unmap(...)
#define cpu_physical_memory_map(a,b,c) ((PVOID)(a))
#define error_report FailCase


#endif
