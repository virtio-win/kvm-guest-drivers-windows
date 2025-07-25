#include "device.h"
#include "assert.h"
#pragma warning(disable : 4201)
#include <setupapi.h>
#include <winioctl.h>
#pragma warning(default : 4201)
#include <string>
#include <regex>

const DWORD LOOKUP_PORT_MAX = 32;

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

BOOL CDevice::Init(BOOL ovrl, UINT portId)
{
    PWCHAR DevicePath;
    std::vector<uint8_t> devpathBuf;
    if ((DevicePath = GetDevicePath(portId, (LPGUID)&GUID_VIOSERIAL_PORT, devpathBuf)) != NULL)
    {
        m_hDevice = CreateFile(DevicePath,
                               GENERIC_WRITE | GENERIC_READ,
                               0,
                               NULL,
                               OPEN_EXISTING,
                               ovrl ? FILE_FLAG_OVERLAPPED : FILE_ATTRIBUTE_NORMAL,
                               NULL);

        if (m_hDevice != INVALID_HANDLE_VALUE)
        {
            printf("Open vioserial device  %S.\n", DevicePath);
            return TRUE;
        }
    }
    DWORD err = GetLastError();
    printf("Cannot find vioserial device PortId:%u, error: %u\n", portId, err);
    return FALSE;
}

BOOL CDevice::Write(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    ULONG ret = 0;
    DWORD bytes = *size;

    if (!buf)
    {
        return FALSE;
    }

    res = WriteFile(m_hDevice, buf, bytes, &ret, NULL);
    if (!res)
    {
        printf("Cannot write vioserial device.\n");
    }
    else if (ret != bytes)
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
    OVERLAPPED ol = {0};

    assert(buf);

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent)
    {
        printf("Event creation failed.\n");
        return FALSE;
    }

    res = WriteFile(m_hDevice, buf, bytes, &ret, &ol);
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

    CloseHandle(ol.hEvent);
    return res;
}

BOOL CDevice::Read(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    DWORD ret;
    DWORD bytes = *size;

    if (!buf)
    {
        return FALSE;
    }

    memset(buf, '\0', bytes);

    res = ReadFile(m_hDevice, buf, bytes, &ret, NULL);
    if (!res)
    {

        printf("PerformReadTest: ReadFile failed: "
               "Error %d\n",
               GetLastError());
    }
    else if (ret != bytes)
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
    OVERLAPPED ol = {0};

    assert(buf);

    memset(buf, '\0', bytes);

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent)
    {
        printf("Event creation failed.\n");
        return FALSE;
    }

    res = ReadFile(m_hDevice, buf, bytes, &ret, &ol);
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

    CloseHandle(ol.hEvent);
    return res;

    if (!res)
    {

        printf("PerformReadTest: ReadFile failed: "
               "Error %d\n",
               GetLastError());
    }
    else if (ret != bytes)
    {
        printf("Read vioserial device error. get = 0x%x, expected = 0x%x\n", ret, bytes);
        *size = ret;
        ret = FALSE;
    }

    return res;
}

BOOL CDevice::GetInfo(PVOID buf, size_t *size)
{
    BOOL res = FALSE;
    DWORD ulOutLength = *size;
    ULONG ulReturnedLength = 0;
    PVOID pBuffer = NULL;
    DWORD err;

    printf("%s, buf = %p, size = %zd\n", __FUNCTION__, buf, *size);
    res = DeviceIoControl(m_hDevice, IOCTL_GET_INFORMATION, NULL, 0, buf, ulOutLength, &ulReturnedLength, NULL);

    if (!res)
    {
        err = GetLastError();
        if (err != ERROR_MORE_DATA)
        {
            printf("Ioctl failed with code %d\n", err);
        }
    }
    *size = ulReturnedLength;
    return res;
}

PTCHAR CDevice::GetDevicePath(UINT portId, IN LPGUID InterfaceGuid, std::vector<uint8_t> &devpathBuf)
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
    ULONG Length, RequiredLength = 0;
    BOOL bResult;

    HardwareDeviceInfo = SetupDiGetClassDevs(InterfaceGuid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE)
    {
        printf("Cannot get class devices.\n");
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD index = 0; index < LOOKUP_PORT_MAX; index++)
    {
        bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo, 0, InterfaceGuid, index, &DeviceInterfaceData);
        if (!bResult)
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_NO_MORE_ITEMS)
            {
                printf("EnumDeviceInterfaces stopped at idx %u\n", index);
                break;
            }
            printf("EnumDeviceInterfaces[%u] error: %u\n", index, dwErr);
            continue;
        }

        bResult = SetupDiGetDeviceInterfaceDetail(HardwareDeviceInfo,
                                                  &DeviceInterfaceData,
                                                  NULL,
                                                  0,
                                                  &RequiredLength,
                                                  NULL);
        if (!bResult)
        {
            DWORD dwErr = GetLastError();
            if (dwErr != ERROR_INSUFFICIENT_BUFFER)
            {
                printf("SetupDiGetDeviceInterfaceDetail[%u] failed with %u\n", index, dwErr);
                continue;
            }
        }

        if (devpathBuf.size() < RequiredLength)
        {
            devpathBuf.resize(RequiredLength);
        }
        DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)devpathBuf.data();
        DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        Length = RequiredLength;

        bResult = SetupDiGetDeviceInterfaceDetail(HardwareDeviceInfo,
                                                  &DeviceInterfaceData,
                                                  DeviceInterfaceDetailData,
                                                  Length,
                                                  &RequiredLength,
                                                  NULL);
        if (!bResult)
        {
            printf("Cannot get device interface details.\n");
            SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
            return NULL;
        }

        std::wstring path(DeviceInterfaceDetailData->DevicePath);
        std::wregex regex(L"&\\d+#");
        std::wsmatch wsm;
        if (!std::regex_search(path, wsm, regex))
        {
            wprintf(L"Failed to parse path: %ls\n", DeviceInterfaceDetailData->DevicePath);
            break;
        }
        // intializing a string without '&' and '#' symbols (ex. "&06#")
        // portIdStr will be "06"
        std::wstring portIdStr(wsm.str(), 1, wsm.str().length() - 1);
        ULONG portIdFromPath;
        try
        {
            portIdFromPath = std::stoi(portIdStr);
        }
        catch (const std::exception &)
        {
            wprintf(L"Could not parse port ID from '%ls'. Skipping.\n", portIdStr.c_str());
            continue;
        }

        if (portIdFromPath == portId)
        {
            printf("Found vio-serial %u\n", portId);
            SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
            return DeviceInterfaceDetailData->DevicePath;
        }
    }

    printf("Cannot find a port with ID:%u\n", portId);
    SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
    return NULL;
}
