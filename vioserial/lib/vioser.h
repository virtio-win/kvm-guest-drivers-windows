#pragma once

#include <windows.h>

#ifdef DLL_EXPORTS
#define DLL_API extern "C" __declspec(dllexport)
#else
#define DLL_API extern "C" __declspec(dllimport)
#endif

typedef BOOL (WINAPI VIOSERIALNOTIFYCALLBACK)(PVOID pRef);

DLL_API BOOL VIOSStartup(void);
DLL_API VOID VIOSCleanup(void);
DLL_API BOOL FindPort(const wchar_t* name);
DLL_API PVOID OpenPortByName(const wchar_t* name);
DLL_API PVOID OpenPortById(UINT id);
DLL_API BOOL ReadPort(PVOID port, PVOID buf, PULONG size);
DLL_API BOOL WritePort(PVOID port, PVOID buf, ULONG size);
DLL_API VOID ClosePort(PVOID port);
DLL_API UINT NumPorts();
DLL_API wchar_t* PortSymbolicName(int index);
DLL_API VOID RegisterNotification(PVOID port, VIOSERIALNOTIFYCALLBACK pfn, PVOID ptr);
