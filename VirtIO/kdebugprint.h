#if defined(IGNORE_VIRTIO_OSDEP_H)
// to make simulation environment easy
#include "external_os_dep.h"
#else

#ifndef _K_DEBUG_PRINT_H
#define _K_DEBUG_PRINT_H

extern int virtioDebugLevel;
extern int bDebugPrint;
typedef void (*tDebugPrintFunc)(const char *format, ...);
extern tDebugPrintFunc VirtioDebugPrintProc;

#define DPrintf(Level, Fmt) if ((!bDebugPrint) || Level > virtioDebugLevel) {} else VirtioDebugPrintProc Fmt

#define DEBUG_ENTRY(level)  DPrintf(level, ("[%s]=>\n", __FUNCTION__))
#define DEBUG_EXIT_STATUS(level, status) DPrintf((status == NDIS_STATUS_SUCCESS ? level : 0), ("[%s]<=0x%X\n", __FUNCTION__, (status)))

#endif
#endif
