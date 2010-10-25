#include "controller.h"

CController::CController(LPCTSTR linkname, LPCTSTR friendlyname)
{
    m_linkname = linkname;
    m_friendlyname = friendlyname;
}

CController::~CController()
{

}

BOOL CController::EnumPorts()
{
    return TRUE;
}

CControllerList::CControllerList(const GUID& guid)
{
    m_guid = guid;
}

CControllerList::~CControllerList()
{

}

int CControllerList::FindControlles()
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    DWORD devindex = 0;

    HardwareDeviceInfo = SetupDiGetClassDevs(
                                 &m_guid,
                                 NULL,
                                 NULL,
                                 (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
                                 );

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE)
    {
        printf("Cannot get class devices.\n");
        return 0;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    for (devindex = 0; SetupDiEnumDeviceInterfaces(HardwareDeviceInfo, NULL, &m_guid, devindex, &DeviceInterfaceData); ++devindex)
    {
        DWORD RequiredLength = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
        SP_DEVINFO_DATA DevInfoData = {sizeof(SP_DEVINFO_DATA)};

        SetupDiGetDeviceInterfaceDetail(
                                 HardwareDeviceInfo,
                                 &DeviceInterfaceData,
                                 NULL,
                                 0,
                                 &RequiredLength,
                                 NULL
                                 );

        DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) malloc(RequiredLength);

        if (DeviceInterfaceDetailData == NULL)
        {
            printf("Cannot allocate memory.\n");
            break;
        }

        DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        SetupDiGetDeviceInterfaceDetail(
                                 HardwareDeviceInfo,
                                 &DeviceInterfaceData,
                                 DeviceInterfaceDetailData,
                                 RequiredLength,
                                 NULL,
                                 &DevInfoData
                                 );
        WCHAR fname[256];
        if (!SetupDiGetDeviceRegistryProperty(HardwareDeviceInfo, &DevInfoData, SPDRP_FRIENDLYNAME, NULL, PBYTE(fname), sizeof (fname), NULL) &&
            !SetupDiGetDeviceRegistryProperty(HardwareDeviceInfo, &DevInfoData, SPDRP_DEVICEDESC, NULL, PBYTE(fname), sizeof (fname), NULL))
        {
            wcscpy(fname, DeviceInterfaceDetailData->DevicePath);
        }
        fname[255] = 0;
        CController controller(DeviceInterfaceDetailData->DevicePath, fname);
        free ((PVOID)DeviceInterfaceDetailData);
        m_controllers.push_back(controller);
    }
    SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
    return static_cast<int>(m_controllers.size());
}
