#include "StdAfx.h"

PnPControl* PnPControl::Instance = NULL;
PnPControl::Reference = 0;

void PnPControl::FindControllers()
{
    wstring name;
    for (int i = 0; FindInstance(GUID_VIOSERIAL_CONTROLLER, i, name); ++i)
    {
        AddController(name.c_str());
    }
}

void PnPControl::FindPorts()
{
    wstring name;
    for (int i = 0; FindInstance(GUID_VIOSERIAL_PORT, i, name); ++i)
    {
        AddPort(name.c_str());
    }
}


BOOL PnPControl::FindInstance(GUID guid, DWORD idx, wstring& name)
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    HardwareDeviceInfo = SetupDiGetClassDevs(
        &guid,
        NULL,
        NULL,
        (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
        );

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE)
    {
        printf("Cannot get class devices.\n");
        return FALSE;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    if (SetupDiEnumDeviceInterfaces(HardwareDeviceInfo, NULL, &guid, idx, &DeviceInterfaceData))
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
            return FALSE;
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
        name = DeviceInterfaceDetailData->DevicePath;
        free ((PVOID)DeviceInterfaceDetailData);
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return TRUE;
    }
    SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
    return FALSE;
}

void PnPControl::Init( )
{
    DWORD id;
    Thread = CreateThread(
        NULL,
        0,
        (LPTHREAD_START_ROUTINE) ServiceThread,
        (LPVOID)this,
        0,
        &id);

    if (Thread == NULL)
    {
        printf("Cannot create thread Error = %d.\n", GetLastError());
    }

    if ( !InitializeCriticalSectionAndSpinCount(&PortsCS, 0x4000))
    {
        printf("Cannot initalize critical section Error = %d.\n", GetLastError());
    }

}

void PnPControl::Close( )
{
    if (PortNotify != NULL)
    {
        UnregisterDeviceNotification(PortNotify);
        PortNotify = NULL;
    }
    if (ControllerNotify != NULL)
    {
        UnregisterDeviceNotification(ControllerNotify);
        ControllerNotify = NULL;
    }
    if (Wnd && Thread)
    {
        SendMessage(Wnd, WM_DESTROY, 0, 0);
        if (WAIT_TIMEOUT == WaitForSingleObject(Thread, 1000))
        {
            printf("Cannot close thread after 1 sec\n");
            TerminateThread(Thread, 0);
        }
        Thread = NULL;
    }
    DeleteCriticalSection(&PortsCS);
}

DWORD WINAPI PnPControl::ServiceThread(PnPControl* ptr)
{
    ptr->Run();
    return 0;
}

void PnPControl::Run()
{
    MSG Msg;
    WNDCLASSEX wc;

    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = 0;
    wc.lpfnWndProc   = GlobalWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = L"VioSerialLib";
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassEx(&wc))
    {
        printf("Window Registration Failed!\n");
    }

    Wnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"VioSerialLib",
        NULL,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, NULL, NULL
        );

    if(Wnd == NULL)
    {
        printf("Window Creation Failed!\n");
    }
    SetWindowLongPtr(Wnd, GWLP_USERDATA, (LONG_PTR)(this));
    ControllerNotify = RegisterInterfaceNotify(GUID_VIOSERIAL_CONTROLLER);
    PortNotify = RegisterInterfaceNotify(GUID_VIOSERIAL_PORT);
    while(GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
}

LRESULT CALLBACK PnPControl::GlobalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_DEVICECHANGE:
        ProcessPnPNotification((PnPControl*)(GetWindowLongPtr(hWnd, GWLP_USERDATA)), PnPNotification(msg, wParam, lParam));
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

HDEVNOTIFY PnPControl::RegisterInterfaceNotify(
    GUID InterfaceClassGuid
    )
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    HDEVNOTIFY Notify;

    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
    NotificationFilter.dbcc_size =
        sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    Notify = RegisterDeviceNotification( Wnd,
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
        );

    if (Notify == NULL)
    {
        printf("RegisterDeviceNotification failed: %d\n",
            GetLastError());
    }
    return Notify;
}

HDEVNOTIFY PnPControl::RegisterHandleNotify(HANDLE handle)
{
    HDEVNOTIFY Notify;
    DEV_BROADCAST_HANDLE NotificationFilter;

    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
    NotificationFilter.dbch_size =
        sizeof(DEV_BROADCAST_HANDLE);
    NotificationFilter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    NotificationFilter.dbch_handle = handle;
    NotificationFilter.dbch_eventguid = GUID_VIOSERIAL_PORT_CHANGE_STATUS;


    Notify = RegisterDeviceNotification( Wnd,
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
        );

    if (Notify == NULL)
    {
        printf("RegisterDeviceNotification failed: %d\n",
            GetLastError());
    }
    return Notify;
}

void PnPControl::ProcessPnPNotification(PnPControl* ptr, PnPNotification Notification)
{
    switch (Notification.wParam)
    {
    case DBT_DEVICEARRIVAL: {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)Notification.lParam;
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            if (IsEqualGUID(GUID_VIOSERIAL_CONTROLLER, pDevInf->dbcc_classguid))
            {
                ptr->AddController(pDevInf->dbcc_name);
            }
            else if (IsEqualGUID(GUID_VIOSERIAL_PORT, pDevInf->dbcc_classguid))
            {
                ptr->AddPort(pDevInf->dbcc_name);
            }
        }
                            }
                            break;
    case DBT_DEVICEREMOVECOMPLETE: {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)Notification.lParam;
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            if (IsEqualGUID(GUID_VIOSERIAL_CONTROLLER, pDevInf->dbcc_classguid))
            {
                ptr->RemoveController(pDevInf->dbcc_name);
            }
            else if (IsEqualGUID(GUID_VIOSERIAL_PORT, pDevInf->dbcc_classguid))
            {
                ptr->RemovePort(pDevInf->dbcc_name);
            }
        }
                                   }
                                   break;
    default:
        ptr->DispatchPnpMessage(Notification);
        break;
    }
}

void PnPControl::AddController(const wchar_t* name)
{
    printf ("Add Controller %ws\n", name);
    Controllers.push_back((new SerialController(name)));
}

void PnPControl::RemoveController(wchar_t* name)
{
    printf ("Remove Controller %ws\n", name);
    for(Iterator it = Controllers.begin(); it != Controllers.end(); it++)
    {
        if (_wcsnicmp((*it)->Name.c_str(), name, (*it)->Name.size()) == 0)
        {
            delete *it;
            Controllers.remove(*it);
            break;
        }
    }
}

void PnPControl::AddPort(const wchar_t* name)
{
    printf ("Add Port %ws\n", name);
    EnterCriticalSection(&PortsCS);
    SerialPort* port = new SerialPort(name, this);
    port->AddRef();
    Ports.push_back(port);
    LeaveCriticalSection(&PortsCS);
}

void PnPControl::RemovePort(wchar_t* name)
{
    printf ("Remove Port %ws\n", name);
    EnterCriticalSection(&PortsCS);
    for(Iterator it = Ports.begin(); it != Ports.end(); it++)
    {
        if (_wcsnicmp((*it)->Name.c_str(), name, (*it)->Name.size()) == 0)
        {
            ((SerialPort*)(*it))->Release();
            Ports.remove(*it);
            break;
        }
    }
    LeaveCriticalSection(&PortsCS);
}

BOOL PnPControl::FindPort(const wchar_t* name)
{
    wstring tmp = name;
    EnterCriticalSection(&PortsCS);
    BOOL ret = FALSE;
    for(Iterator it = Ports.begin(); it != Ports.end(); it++)
    {
        if (_wcsnicmp((*it)->SymbolicName.c_str(), name, (*it)->Name.size()) == 0)
        {
            ret = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&PortsCS);
    return ret;
}
PVOID PnPControl::OpenPortByName(const wchar_t* name)
{
    wstring tmp = name;
    PVOID ret = NULL;
    EnterCriticalSection(&PortsCS);
    for(Iterator it = Ports.begin(); it != Ports.end(); it++)
    {
        if (_wcsnicmp((*it)->SymbolicName.c_str(), name, (*it)->Name.size()) == 0)
        {
            SerialPort* port = (SerialPort*)(*it);
            if (port->OpenPort() == TRUE)
            {
                port->AddRef();
                ret = port;
                break;
            }
        }
    }
    LeaveCriticalSection(&PortsCS);
    return ret;
}
PVOID PnPControl::OpenPortById(UINT id)
{
    PVOID ret = NULL;
    EnterCriticalSection(&PortsCS);
    if ((size_t)(id) < NumPorts())
    {
        Iterator it = Ports.begin();
        advance(it, id);
        SerialPort* port = (SerialPort*)(*it);
        if (port->OpenPort() == TRUE)
        {
            port->AddRef();
            ret = port;
        }
    }
    LeaveCriticalSection(&PortsCS);
    return ret;
}
BOOL PnPControl::ReadPort(PVOID port, PVOID buf, PULONG size)
{
    Iterator it;
    EnterCriticalSection(&PortsCS);
    for(it = Ports.begin(); it != Ports.end(); it++)
    {
        if (*it == port)
        {
            break;
        }
    }
    LeaveCriticalSection(&PortsCS);
    return (*it != port) ? FALSE : ((SerialPort*)(*it))->ReadPort(buf, (size_t*)(size));
}
BOOL PnPControl::WritePort(PVOID port, PVOID buf, ULONG size)
{
    Iterator it;
    EnterCriticalSection(&PortsCS);
    for(it = Ports.begin(); it != Ports.end(); it++)
    {
        if (*it == port)
        {
            break;
        }
    }
    LeaveCriticalSection(&PortsCS);
    return (*it != port) ? FALSE : ((SerialPort*)(*it))->WritePort(buf, (size_t*)(&size));
}
VOID PnPControl::ClosePort(PVOID port)
{
    EnterCriticalSection(&PortsCS);
    for(Iterator it = Ports.begin(); it != Ports.end(); it++)
    {
        if (*it == port)
        {
            SerialPort* port = (SerialPort*)(*it);
            port->ClosePort();
            port->Release();
            break;
        }
    }
    LeaveCriticalSection(&PortsCS);
}

wchar_t* PnPControl::PortSymbolicName(int index)
{
    wchar_t* ret = NULL;
    EnterCriticalSection(&PortsCS);
    if ((size_t)(index) < NumPorts())
    {
        Iterator it = Ports.begin();
        advance(it, index);
        ret = (wchar_t*)((*it)->SymbolicName.c_str());
    }
    LeaveCriticalSection(&PortsCS);
    return ret;
}

VOID PnPControl::RegisterNotification(PVOID port, VIOSERIALNOTIFYCALLBACK pfn, PVOID ptr)
{
    for(Iterator it = Ports.begin(); it != Ports.end(); it++)
    {
        if (*it == port)
        {
            ((SerialPort*)(*it))->NotificationPair.first = pfn;
            ((SerialPort*)(*it))->NotificationPair.second = ptr;
            return;
        }
    }
}

BOOL PnPControl::IsRunningAsService()
{
    wchar_t wstrPath[_MAX_FNAME];
    if (!GetModuleFileName( NULL, wstrPath, _MAX_FNAME))
    {
        printf ("Error getting module file name (%d)\n", GetLastError());
        return FALSE;
    }
    printf ("Module File Name is %ws\n", wstrPath);

    wstring fullname(wstrPath);
    size_t pos_begin = fullname.rfind(L'\\') + 1;
    size_t pos_end   = fullname.rfind(L'.');
    wstring filename(fullname, pos_begin, pos_end-pos_begin);
    printf ("File name = %ws\n", filename.c_str());
    SERVICE_STATUS_PROCESS status;
    DWORD                  needed;

    SC_HANDLE scm = OpenSCManager (NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm == NULL)
    {
        printf ("OpenSCManager failed (%d)\n", GetLastError());
        return FALSE;
    }
    SC_HANDLE svc = OpenService (scm, filename.c_str(), SERVICE_ALL_ACCESS);
    if (svc == NULL)
    {
        printf ("OpenService failed (%d)\n", GetLastError());
        return FALSE;
    }
    if(!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE) &status, sizeof(SERVICE_STATUS_PROCESS), &needed))
    {
        printf ("QueryServiceStatusEx failed (%d)\n", GetLastError());
        CloseServiceHandle (svc);
        CloseServiceHandle (scm);
        return FALSE;
    }

    CloseServiceHandle (svc);
    CloseServiceHandle (scm);
    return (status.dwProcessId == GetCurrentProcessId());
}
