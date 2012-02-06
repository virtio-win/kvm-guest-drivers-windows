#if defined(IGNORE_VIRTIO_OSDEP_H)
// to make simulation environment easy
#include "external_os_dep.h"
#else

#ifndef _K_DEBUG_PRINT_H
#define _K_DEBUG_PRINT_H

#include <ntddk.h>
#include <stdarg.h>

int
_cdecl
_vsnprintf(
    char *buf,
    size_t size,
    const char *fmt,
    va_list args
    );
#define vsnprintf _vsnprintf

#ifndef TRACE_LEVEL_INFORMATION
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_FATAL       1   // Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif // TRACE_LEVEL_INFORMATION

extern int nDebugLevel;
extern int bDebugPrint;

//
// Just use the base of COM1
//
#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)

ULONG
_cdecl
RhelDbgPrintToComPort(
    IN LPTSTR Format,
    ...
    );

#if DBG
#if RHEL_COM_DEBUG
#define RhelDbgPrint(__LEVEL__,__MSG__) \
    if (nDebugLevel >= (__LEVEL__)) {  \
        RhelDbgPrintToComPort __MSG__;   \
    }
#else
#define RhelDbgPrint(__LEVEL__,__MSG__) \
    if (nDebugLevel >= (__LEVEL__)) {  \
        DbgPrint __MSG__;               \
    }
#endif RHEL_COM_DEBUG
#else DBG
#define RhelDbgPrint(__LEVEL__,__MSG__)
#endif DBG

#define DPrintf(Level, Fmt) \
{ \
    if ((1==bDebugPrint) && Level <= nDebugLevel) \
    { \
        DbgPrint Fmt; \
    } \
	else \
	    if(2==bDebugPrint) \
        { \
            RhelDbgPrint(Level, Fmt);	\
        } \
}

#define DPrintFunctionName(Level) DPrintf(Level, ("%s\n", __FUNCTION__))

void InitializeDebugPrints(PUNICODE_STRING RegistryPath);

#endif
#endif
