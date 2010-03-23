#include "device.h"
#include "public.h"

CDevice::CDevice()
{
    m_hDevice = INVALID_HANDLE_VALUE;
}

CDevice::~CDevice()
{
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
}

BOOL CDevice::Init()
{
    PWCHAR DevicePath = NULL;
    if ((DevicePath = GetDevicePath((LPGUID)&GUID_DEVINTERFACE_BALLOON)) != NULL) {
        m_hDevice = CreateFile(DevicePath,
                             GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED,
                             NULL );

        if (m_hDevice != INVALID_HANDLE_VALUE) {
            return TRUE;
        }

    }
    return FALSE;
}

BOOL CDevice::Write(PBALLOON_STAT pstat, int nr, HANDLE ev)
{
    OVERLAPPED ov = {0};
    ov.hEvent = ev;
    return WriteFile(m_hDevice, pstat, sizeof(BALLOON_STAT) * nr, NULL, &ov);
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
                             (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
                                              0,
                                              InterfaceGuid,
                                              0,
                                              &DeviceInterfaceData);

    if (bResult == FALSE) {
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

    if (DeviceInterfaceDetailData == NULL) {
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
                  NULL);

    if (bResult == FALSE) {
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        LocalFree(DeviceInterfaceDetailData);
        return NULL;
    }

    return DeviceInterfaceDetailData->DevicePath;
}
