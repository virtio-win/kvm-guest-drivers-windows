#include "device.h"
#include "public.h"
#include "utils.h"

CDevice::CDevice()
{
    m_hDevice = INVALID_HANDLE_VALUE;
}

CDevice::~CDevice()
{
    BALLOON_STAT stat[VIRTIO_BALLOON_S_NR];
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        memset(stat, -1, sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR);
        Write(stat, VIRTIO_BALLOON_S_NR);
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
                             FILE_ATTRIBUTE_NORMAL,
                             NULL );

        if (m_hDevice != INVALID_HANDLE_VALUE) {
            PrintMessage("Open balloon device");
            return TRUE;
        }

    }
    PrintMessage("Cannot find balloon device");
    return FALSE;
}

BOOL CDevice::Write(PBALLOON_STAT pstat, int nr)
{
    BOOL res = FALSE;
    ULONG ret = 0;
    res = WriteFile(m_hDevice, pstat, sizeof(BALLOON_STAT) * nr, &ret, NULL);
    if (!res) {
        PrintMessage("Cannot write balloon device");
    } else if ( ret != sizeof(BALLOON_STAT) * nr) {
        PrintMessage("Write balloon device error");
        ret = FALSE;
    }
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
                             (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE) {
        PrintMessage("Cannot get class devices");
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
                                              0,
                                              InterfaceGuid,
                                              0,
                                              &DeviceInterfaceData);

    if (bResult == FALSE) {
        PrintMessage("Cannot get enumerate device interfaces");
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
        PrintMessage("Cannot allocate memory");
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
        PrintMessage("Cannot get device interface details");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        LocalFree(DeviceInterfaceDetailData);
        return NULL;
    }

    return DeviceInterfaceDetailData->DevicePath;
}
