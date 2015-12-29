#include "StdAfx.h"

HINSTANCE  ghInstance = NULL;      // module handle.
PnPControl* control = NULL;


BOOL APIENTRY DllMain (HANDLE hModule, ULONG ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(ul_reason_for_call);
    UNREFERENCED_PARAMETER(lpReserved);

    ghInstance = (HINSTANCE) hModule;
    return TRUE;
}

DLL_API BOOL VIOSStartup(void)
{
    PnPControl::GetInstance();
    return TRUE;
}

DLL_API VOID VIOSCleanup ( void )
{
    PnPControl::CloseInstance();
}

DLL_API BOOL FindPort ( const wchar_t* name )
{
    PnPControl* control = PnPControl::GetInstance();
    BOOL ret = control->FindPort(name);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API PVOID OpenPortByName ( const wchar_t* name )
{
    PnPControl* control = PnPControl::GetInstance();
    PVOID ret = control->OpenPortByName(name);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API PVOID OpenPortById ( UINT id )
{
    PnPControl* control = PnPControl::GetInstance();
    PVOID ret = control->OpenPortById(id);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API BOOL ReadPort ( PVOID port, PVOID buf, PULONG size )
{
    PnPControl* control = PnPControl::GetInstance();
    BOOL ret = control->ReadPort(port, buf, size);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API BOOL WritePort ( PVOID port, PVOID buf, ULONG size )
{
    PnPControl* control = PnPControl::GetInstance();
    BOOL ret = control->WritePort(port, buf, size);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API VOID ClosePort ( PVOID port )
{
    PnPControl* control = PnPControl::GetInstance();
    control->ClosePort(port);
    PnPControl::CloseInstance();
}

DLL_API UINT NumPorts(void)
{
    PnPControl* control = PnPControl::GetInstance();
    UINT ret = (UINT)control->NumPorts();
    PnPControl::CloseInstance();
    return ret;
}

DLL_API wchar_t* PortSymbolicName ( int index )
{
    PnPControl* control = PnPControl::GetInstance();
    wchar_t* ret = control->PortSymbolicName(index);
    PnPControl::CloseInstance();
    return ret;
}

DLL_API VOID RegisterNotification(PVOID port, VIOSERIALNOTIFYCALLBACK pfn, PVOID ptr)
{
    PnPControl* control = PnPControl::GetInstance();
    control->RegisterNotification(port, pfn, ptr);
    PnPControl::CloseInstance();
}
