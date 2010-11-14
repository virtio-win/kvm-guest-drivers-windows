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
    control = (PnPControl*) new PnPControl();
    return TRUE;
}

DLL_API VOID VIOSCleanup ( void )
{
    delete control;
    control = NULL;
}

DLL_API BOOL FindPort ( const wchar_t* name )
{
    return control->FindPort(name);
}

DLL_API PVOID OpenPort ( const wchar_t* name )
{
    return control->OpenPort(name);
}

DLL_API BOOL ReadPort ( PVOID port, PVOID buf, PULONG size )
{
    return control->ReadPort(port, buf, size);
}

DLL_API BOOL WritePort ( PVOID port, PVOID buf, ULONG size )
{
    return control->WritePort(port, buf, size);
}

DLL_API VOID ClosePort ( PVOID port )
{
    return control->ClosePort(port);
}

DLL_API UINT NumPorts(void)
{
    return control->NumPorts();
}

DLL_API wchar_t* PortSymbolicName ( int index )
{
    return control->PortSymbolicName(index);
}

