#ifndef _K_DEBUG_PRINT_H
#define _K_DEBUG_PRINT_H

extern int nDebugLevel;
extern int bDebugPrint;

#define DPrintf(Level, Fmt) \
{ \
    if (bDebugPrint && Level <= nDebugLevel) \
    { \
        DbgPrint Fmt; \
    } \
}

#define DPrintFunctionName(Level) DPrintf(Level, ("%s\n", __FUNCTION__))

void InitializeDebugPrints(PUNICODE_STRING RegistryPath);

#endif
