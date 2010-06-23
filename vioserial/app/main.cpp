#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include "..\sys\public.h"
#pragma warning(disable:4201)

#include <setupapi.h>
#include <winioctl.h>

#pragma warning(default:4201)

//-----------------------------------------------------------------------------
// 4127 -- Conditional Expression is Constant warning
//-----------------------------------------------------------------------------
#define WHILE(constant) \
__pragma(warning(disable: 4127)) while(constant); __pragma(warning(default: 4127))

BOOLEAN
PerformReadTest(
    IN HANDLE hDevice
    )
{
    PUCHAR ReadBuffer = NULL;
    BOOLEAN result = TRUE;
    DWORD bytesReturned;
    int i;
    ReadBuffer = (PUCHAR)malloc(1024);
    if( ReadBuffer == NULL ) {

        printf("PerformReadTest: Could not allocate %d "
               "bytes ReadBuffer\n",1024);

         result = FALSE;
         goto Cleanup;

    }

    bytesReturned = 0;

    memset(ReadBuffer, '\0', 1024);

    if (!ReadFile ( hDevice,
            ReadBuffer,
            1024,
            &bytesReturned,
            NULL)) {

        printf ("PerformReadTest: ReadFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    } else {


    }
    printf ("%s\n",
                ReadBuffer);

Cleanup:

    if (ReadBuffer) {
        free (ReadBuffer);
    }
    return result;
}

BOOLEAN
PerformWriteTest(
    IN HANDLE hDevice
    )
{
    PUCHAR WriteBuffer = NULL;
    BOOLEAN result = TRUE;
    DWORD bytesReturned;
    int i;
    WriteBuffer = (PUCHAR)malloc(1024);
    if( WriteBuffer == NULL ) {

        printf("PerformWriteTest: Could not allocate %d "
               "bytes WriteBuffer\n",1024);

         result = FALSE;
         goto Cleanup;
    }

    bytesReturned = 0;

    memset(WriteBuffer, '\n', 1024);

    for(i = 0; i < 1024; i++)
    {
        int ch = getchar(); 

        if(ch == 13) {
           ++i; 
           break;
        }
        WriteBuffer[i] = (char)ch;
    }
    if (!WriteFile ( hDevice,
            WriteBuffer,
            i,
            &bytesReturned,
            NULL)) {

        printf ("PerformWriteTest: WriteFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    } else {

        if( bytesReturned != i ) {

            printf("bytes written is not test length! Written %d, "
                   "SB %d\n",bytesReturned, i);

            result = FALSE;
            goto Cleanup;
        }

        printf ("%d Pattern Bytes Written successfully\n",
                bytesReturned);
    }

Cleanup:

    if (WriteBuffer) {
        free (WriteBuffer);
    }
    return result;
}


ULONG
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(Argc) PWCHAR argv[]
    )
{
    HDEVINFO                            hardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA            deviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    deviceInterfaceDetailData = NULL;
    ULONG                               predictedLength = 0;
    ULONG                               requiredLength = 0, bytes=0;
    HANDLE                              file;
    ULONG                               i =0;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    hardwareDeviceInfo = SetupDiGetClassDevs (
                       (LPGUID)&GUID_VIOSERIAL_PORT,
                       NULL, // Define no enumerator (global)
                       NULL, // Define no
                       (DIGCF_PRESENT | // Only Devices present
                       DIGCF_DEVICEINTERFACE)); // Function class devices.
    if(INVALID_HANDLE_VALUE == hardwareDeviceInfo)
    {
        printf("SetupDiGetClassDevs failed: %x\n", GetLastError());
        return 0;
    }

    deviceInterfaceData.cbSize = sizeof (SP_DEVICE_INTERFACE_DATA);

    printf("\nList of VISERIAL PORT Device Interfaces\n");
    printf("---------------------------------\n");

    i = 0;

    do {
        if (SetupDiEnumDeviceInterfaces (hardwareDeviceInfo,
                                 0, // No care about specific PDOs
                                 (LPGUID)&GUID_VIOSERIAL_PORT,
                                 i, //
                                 &deviceInterfaceData)) {

            if(deviceInterfaceDetailData) {
                free (deviceInterfaceDetailData);
                deviceInterfaceDetailData = NULL;
            }

            if(!SetupDiGetDeviceInterfaceDetail (
                    hardwareDeviceInfo,
                    &deviceInterfaceData,
                    NULL, // probing so no output buffer yet
                    0, // probing so output buffer length of zero
                    &requiredLength,
                    NULL)) {
                if(ERROR_INSUFFICIENT_BUFFER != GetLastError()) {
                    printf("SetupDiGetDeviceInterfaceDetail failed %d\n", GetLastError());
                    SetupDiDestroyDeviceInfoList (hardwareDeviceInfo);
                    return FALSE;
                }

            }

            predictedLength = requiredLength;

            deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc (predictedLength);

            if(deviceInterfaceDetailData) {
                deviceInterfaceDetailData->cbSize =
                                sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA);
            } else {
                printf("Couldn't allocate %d bytes for device interface details.\n", predictedLength);
                SetupDiDestroyDeviceInfoList (hardwareDeviceInfo);
                return FALSE;
            }


            if (! SetupDiGetDeviceInterfaceDetail (
                       hardwareDeviceInfo,
                       &deviceInterfaceData,
                       deviceInterfaceDetailData,
                       predictedLength,
                       &requiredLength,
                       NULL)) {
                printf("Error in SetupDiGetDeviceInterfaceDetail\n");
                SetupDiDestroyDeviceInfoList (hardwareDeviceInfo);
                free (deviceInterfaceDetailData);
                return FALSE;
            }
            printf("%d) %s\n", ++i,
                    deviceInterfaceDetailData->DevicePath);
        }
        else if (ERROR_NO_MORE_ITEMS != GetLastError()) {
            free (deviceInterfaceDetailData);
            deviceInterfaceDetailData = NULL;
            continue;
        }
        else
            break;

    } WHILE (TRUE);


    SetupDiDestroyDeviceInfoList (hardwareDeviceInfo);

    if(!deviceInterfaceDetailData)
    {
        printf("No device interfaces present\n");
        return 0;
    }

    printf("\nOpening the last interface:\n %s\n",
                    deviceInterfaceDetailData->DevicePath);

    file = CreateFile ( deviceInterfaceDetailData->DevicePath,
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        OPEN_EXISTING, // No special create flags
                        0,
                        NULL);

    if (INVALID_HANDLE_VALUE == file) {
        printf("Error in CreateFile: %x", GetLastError());
        free (deviceInterfaceDetailData);
        return 0;
    }
/*
    if(!PerformWriteTest(file)) { 
        printf("WriteTest request failed:0x%x\n", GetLastError());
        free (deviceInterfaceDetailData);
        CloseHandle(file);
        return 0;
    }

    printf("WriteTest completed successfully\n");
*/
    if(!PerformReadTest(file)) { 
        printf("WriteTest request failed:0x%x\n", GetLastError());
        free (deviceInterfaceDetailData);
        CloseHandle(file);
        return 0;
    }

    printf("PerformReadTest completed successfully\n");

    free (deviceInterfaceDetailData);
    CloseHandle(file);
    return 0;
}
