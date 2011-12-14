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

NETCO_API DWORD CALLBACK NetCoinstaller (__in DI_FUNCTION InstallFunction,
                                         __in HDEVINFO DeviceInfoSet,
                                         __in PSP_DEVINFO_DATA DeviceInfoData,
                                         OPTIONAL __inout PCOINSTALLER_CONTEXT_DATA Context);

