#pragma once

// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the NETCO_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// NETCO_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef NETCO_EXPORTS
#define NETCO_API __declspec(dllexport)
#else
#define NETCO_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

DWORD NETCO_API InitHelperDll(__in DWORD dwNetshVersion,
                                   PVOID pReserved);

//Register NetKVM NetSH helper in NetSH framework
//Returns ERROR_SUCCESS or corresponding error value
//WARNING: Uses DLL file pathname,
//         must be called after DLL copied to its final location
DWORD NETCO_API RegisterNetKVMNetShHelper(void);
//Unregister RedHat NetSH helper
//Returns ERROR_SUCCESS or corresponding error value
DWORD NETCO_API UnregisterNetKVMNetShHelper(void);

#ifdef __cplusplus
} // extern "C"
#endif
