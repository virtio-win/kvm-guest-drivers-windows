#include <wdm.h>

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
    UNREFERENCED_PARAMETER(pDriverObject);
    UNREFERENCED_PARAMETER(pRegistryPath);
    return STATUS_NOT_SUPPORTED;
}
