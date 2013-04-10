#ifndef osdep_sim_h
#define osdep_sim_h

#include <Windows.h>
#include <stdio.h>

#define PAGE_SIZE       0x1000
#define PAGE_SHIFT      12

#ifdef __cplusplus
#else
typedef BOOLEAN bool;
#endif
typedef ULONG u32, uint32_t;
typedef UCHAR u8, uint8_t;
typedef USHORT u16;
typedef ULONGLONG u64;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;

EXTERN_C int debugLevel;

EXTERN_C void DoPrint(const char *format, ...);

#define DPrintf(level, statement) if (debugLevel < level) {} else DoPrint statement

#define wmb()
#define mb()
#define rmb()

#define malloc AllocateMemory
#define free   DeallocateMemory

EXTERN_C void *AllocateMemory(size_t size);
EXTERN_C void DeallocateMemory(void *);

#endif
