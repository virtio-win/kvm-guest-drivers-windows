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

#endif
#endif
