#include "precomp.h"


HINSTANCE                   ghInstance = NULL;      // module handle.



BOOL APIENTRY DllMain (HANDLE hModule, ULONG ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(ul_reason_for_call);
    UNREFERENCED_PARAMETER(lpReserved);

    ghInstance = (HINSTANCE) hModule;
    return TRUE;
}
