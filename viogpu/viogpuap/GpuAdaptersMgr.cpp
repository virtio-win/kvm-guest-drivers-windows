#include "pch.h"

void GpuAdaptersMgr::FindAdapters()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    std::wstring id = TEXT("PCI\\VEN_1AF4&DEV_1050");
    DISPLAY_DEVICE adapter = { sizeof(DISPLAY_DEVICE) };
    DWORD adapterIndex = 0;
    while (FindDisplayDevice(&adapter, id, &adapterIndex))
    {
        if (adapter.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            AddAdapter(adapter.DeviceName);
        }
    }
}

BOOL GpuAdaptersMgr::GetDisplayDevice(
    LPCTSTR lpDevice,
    DWORD iDevNum,
    PDISPLAY_DEVICE lpDisplayDevice,
    DWORD dwFlags)
{
    PrintMessage(L"%ws iDevNum = %d\n", __FUNCTIONW__, iDevNum);

    return ::EnumDisplayDevices(lpDevice, iDevNum, lpDisplayDevice, dwFlags);
};

BOOL GpuAdaptersMgr::FindDisplayDevice(PDISPLAY_DEVICE lpDisplayDevice,
    std::wstring& name, PDWORD adapterIndex)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD index = *adapterIndex;
    (*adapterIndex)++;

    if (GetDisplayDevice(NULL, index, lpDisplayDevice, 0)) {
        if (!name.empty() &&
            !_wcsnicmp(lpDisplayDevice->DeviceID, name.c_str(), name.size()))
        {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GpuAdaptersMgr::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_hThread = CreateThread(
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)ServiceThread,
        (LPVOID)this,
        0,
        NULL);

    if (m_hThread == NULL)
    {
        PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
        return FALSE;
    }

    FindAdapters();

    return TRUE;
}

void GpuAdaptersMgr::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    RemoveAllAdapters();

    if (m_hAdapterNotify != NULL)
    {
        UnregisterDeviceNotification(m_hAdapterNotify);
        m_hAdapterNotify = NULL;
    }

    if (m_hWnd && m_hThread)
    {
        SendMessage(m_hWnd, WM_DESTROY, 0, 0);
        if (WAIT_TIMEOUT == WaitForSingleObject(m_hThread, 1000))
        {
            PrintMessage(L"Cannot close thread after 1 sec\n");
            TerminateThread(m_hThread, 0);
        }
        m_hThread = NULL;
    }
}

DWORD WINAPI GpuAdaptersMgr::ServiceThread(GpuAdaptersMgr* ptr)
{
    ptr->Run();
    return 0;
}

#define WIN_CLASS_NAME  TEXT("VioGpuMon")

void GpuAdaptersMgr::Run()
{
    MSG Msg;
    WNDCLASSEX wc = {0};
    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = GlobalWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hCursor = NULL;
    wc.hIcon = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WIN_CLASS_NAME;
    wc.hIconSm = NULL;

    if (!RegisterClassEx(&wc))
    {
        PrintMessage(L"Window Registration Failed!\n");
        return;
    }

    m_hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        WIN_CLASS_NAME,
        NULL,
        WS_POPUP,
        0, 0, 0, 0,
        HWND_DESKTOP, NULL, hInstance, NULL
    );

    if (m_hWnd == NULL)
    {
        PrintMessage(L"Window Creation Failed!\n");
        return;
    }

    SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (LONG_PTR)(this));
    m_hAdapterNotify = RegisterInterfaceNotify(GUID_DEVCLASS_DISPLAY);

    while (GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
}

LRESULT CALLBACK GpuAdaptersMgr::GlobalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
        ProcessPnPNotification((GpuAdaptersMgr*)(GetWindowLongPtr(hWnd, GWLP_USERDATA)), Notification(msg, wParam, lParam));
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

HDEVNOTIFY GpuAdaptersMgr::RegisterInterfaceNotify(
    GUID InterfaceClassGuid
)
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    HDEVNOTIFY Notify;

    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size =
        sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    Notify = RegisterDeviceNotification(m_hWnd,
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if (Notify == NULL)
    {
        PrintMessage(L"RegisterDeviceNotification failed: %d\n",
            GetLastError());
    }
    return Notify;
}

void GpuAdaptersMgr::ProcessPnPNotification(GpuAdaptersMgr* ptr, Notification notification)
{
    switch (notification.wParam)
    {
    case DBT_DEVICEARRIVAL: {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)notification.lParam;
        PrintMessage(L"DBT_DEVICEARRIVAL\n");
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            if (IsEqualGUID(GUID_DEVCLASS_DISPLAY, pDevInf->dbcc_classguid))
            {
                ptr->FindAdapters();
            }
        }
    }
    break;
    case DBT_DEVICEREMOVECOMPLETE: {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)notification.lParam;
        PrintMessage(L"DBT_DEVICEREMOVECOMPLETE\n");
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            if (IsEqualGUID(GUID_DEVCLASS_DISPLAY, pDevInf->dbcc_classguid))
            {
                ptr->InvalidateAdapters();
            }
        }
    }
    break;
    case DBT_DEVNODES_CHANGED: {
        PrintMessage(L"DBT_DEVNODES_CHANGED\n");
        ptr->InvalidateAdapters();
    }
    break;
    default:
    break;
    }
}

void GpuAdaptersMgr::AddAdapter(const wchar_t* name)
{
    PrintMessage(L"Add Adapter %ws\n", name);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        if (_wcsnicmp((*it)->m_DeviceName.c_str(), name, (*it)->m_DeviceName.size()) == 0)
        {
            (*it)->SetStatus(Active);
            return;
        }
    }

    Adapters.push_back(new GpuAdapter(name));
}

void GpuAdaptersMgr::RemoveAllAdapters()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        delete *it;
    }

    Adapters.clear();
}

void GpuAdaptersMgr::InvalidateAdapters()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        (*it)->SetStatus(Reset);
    }

    FindAdapters();

    Iterator it = Adapters.begin();
    while (it != Adapters.end())
    {
        if ((*it)->GetStatus() == Reset)
        {
            delete *it;
            Adapters.erase(it);
            it = Adapters.begin();
        }
        else {
            ++it;
        }
    }
}
