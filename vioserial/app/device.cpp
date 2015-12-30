#include "device.h"
#include "assert.h"

CDevice::CDevice()
{
    m_hDevice = INVALID_HANDLE_VALUE;
}

CDevice::~CDevice()
{
    if (m_hDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
}

BOOL CDevice::Init(BOOL ovrl)
{
    PWCHAR DevicePath = NULL;
    if ((DevicePath = GetDevicePath((LPGUID)&GUID_VIOSERIAL_PORT)) != NULL)
    {
        m_hDevice = CreateFile(DevicePath,
                             GENERIC_WRITE | GENERIC_READ,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             ovrl ? FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL,
                             NULL );

        if (m_hDevice != INVALID_HANDLE_VALUE)
        {
            printf("Open vioserial device  %S.\n", DevicePath);
            return TRUE;
        }

    }
    DWORD err = GetLastError();
    printf("Cannot find vioserial device. %S , error = %d\n", DevicePath, err );
    return FALSE;
}

BOOL CDevice::Write(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    ULONG ret = 0;
    DWORD bytes = *size;

    if (!buf) return FALSE;

    res = WriteFile ( m_hDevice,
                      buf,
                      bytes,
                      &ret,
                      NULL
                     );
    if (!res)
    {
        printf("Cannot write vioserial device.\n");
    }
    else if ( ret != bytes)
    {
        printf("Write vioserial device error. written = 0x%x, expected = 0x%x\n", ret, bytes);
        *size = ret;
        ret = FALSE;
    }
    return res;
}

BOOL CDevice::WriteEx(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    ULONG ret = 0;
    DWORD bytes = *size;
    OVERLAPPED  ol = {0};

    assert( buf );

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert( ol.hEvent );

    res = WriteFile ( m_hDevice,
                      buf,
                      bytes,
                      &ret,
                      &ol
                     );
    if (!res)
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
           printf("Write failed but isn't delayed.\n");
           res = FALSE;
        }
        else
        {
           if (!GetOverlappedResult(m_hDevice, &ol, &ret, TRUE))
           {
              res = FALSE;
           }
           else
           {
              *size = ret;
              res = TRUE;
           }
        }
    }
    else
    {
        *size = ret;
        res = TRUE;
    }

    CloseHandle( ol.hEvent );
    return res;
}

BOOL CDevice::Read(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    DWORD ret;
    DWORD bytes = *size;

    if (!buf) return FALSE;

    memset(buf, '\0', bytes);

    res = ReadFile ( m_hDevice,
                    buf,
                    bytes,
                    &ret,
                    NULL
                   );
    if (!res)
    {

        printf ("PerformReadTest: ReadFile failed: "
                "Error %d\n", GetLastError());
    }
    else if ( ret != bytes)
    {
        printf("Read vioserial device error. get = 0x%x, expected = 0x%x\n", ret, bytes);
        *size = ret;
        ret = FALSE;
    }

    return res;
}

BOOL CDevice::ReadEx(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    DWORD ret;
    DWORD bytes = *size;
    OVERLAPPED  ol = {0};

    assert(buf);

    memset(buf, '\0', bytes);

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(ol.hEvent);

    res = ReadFile ( m_hDevice,
                    buf,
                    bytes,
                    &ret,
                    &ol
                   );
    if (!res)
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
           printf("Write failed but isn't delayed.\n");
           res = FALSE;
        }
        else
        {
           if (!GetOverlappedResult(m_hDevice, &ol, &ret, TRUE))
           {
              res = FALSE;
           }
           else
           {
              *size = ret;
              res = TRUE;
           }
        }
    }
    else
    {
        *size = ret;
        res = TRUE;
    }

    CloseHandle( ol.hEvent );
    return res;













    if (!res)
    {

        printf ("PerformReadTest: ReadFile failed: "
                "Error %d\n", GetLastError());
    }
    else if ( ret != bytes)
    {
        printf("Read vioserial device error. get = 0x%x, expected = 0x%x\n", ret, bytes);
        *size = ret;
        ret = FALSE;
    }

    return res;
}

BOOL CDevice::GetInfo(PVOID buf, size_t *size)
{
    BOOL    res = FALSE;
    DWORD   ulOutLength = *size;
    ULONG   ulReturnedLength = 0;
    PVOID   pBuffer = NULL;
    DWORD   err;

    printf ("%s, buf = %p, size = %d\n", __FUNCTION__, buf, *size);
    res = DeviceIoControl(
                             m_hDevice,
                             IOCTL_GET_INFORMATION,
                             NULL,
                             0,
                             buf,
                             ulOutLength,
                             &ulReturnedLength,
                             NULL
                             );

    if ( !res )
    {   err = GetLastError();
        if (err != ERROR_MORE_DATA)
        {
           printf("Ioctl failed with code %d\n", err );
        }
    }
    *size = ulReturnedLength;
    return res;
}

PTCHAR CDevice::GetDevicePath( IN  LPGUID InterfaceGuid )
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
    ULONG Length, RequiredLength = 0;
    BOOL bResult;

    HardwareDeviceInfo = SetupDiGetClassDevs(
                             InterfaceGuid,
                             NULL,
                             NULL,
                             (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
                             );

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE)
    {
        printf("Cannot get class devices.\n");
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
                             0,
                             InterfaceGuid,
                             0,
                             &DeviceInterfaceData
                             );

    if (bResult == FALSE) {
        printf("Cannot get enumerate device interfaces.\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    SetupDiGetDeviceInterfaceDetail(
        HardwareDeviceInfo,
        &DeviceInterfaceData,
        NULL,
        0,
        &RequiredLength,
        NULL
        );

    DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, RequiredLength);

    if (DeviceInterfaceDetailData == NULL)
    {
        printf("Cannot allocate memory.\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    Length = RequiredLength;

    bResult = SetupDiGetDeviceInterfaceDetail(
                             HardwareDeviceInfo,
                             &DeviceInterfaceData,
                             DeviceInterfaceDetailData,
                             Length,
                             &RequiredLength,
                             NULL
                             );

    if (bResult == FALSE)
    {
        printf("Cannot get device interface details.\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        LocalFree(DeviceInterfaceDetailData);
        return NULL;
    }

    return DeviceInterfaceDetailData->DevicePath;
}
