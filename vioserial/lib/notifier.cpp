#include "notifier.h"

NotifierMap CNotifier::m_map;
#define WM_REGISTER_NOTIFIER WM_USER+123

CNotifier::CNotifier()
{
    m_hThread  = INVALID_HANDLE_VALUE;
	m_hDeviceNotify = NULL;
	m_szClassName = L"NotifierWindowClass";
}

CNotifier::~CNotifier()
{
    if (m_hThread != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hThread);
        m_hThread = INVALID_HANDLE_VALUE;
    }
}

BOOL CNotifier::Init()
{
    DWORD id;

    m_hThread = CreateThread(
                              NULL,
                              0,
                              (LPTHREAD_START_ROUTINE) ServiceThread,
                              (LPVOID)this,
                              0,
                              &id);

    if (m_hThread == NULL) {
        printf("Cannot create thread.\n"); 
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI CNotifier::ServiceThread(CNotifier* ptr)
{
    ptr->Run();
    return 0;
}

void CNotifier::Run()
{
    MSG Msg;
	if(!Create())
		return;

    // Step 3: The Message Loop
    while(GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
}

BOOL CNotifier::Create()
{
    WNDCLASSEX wc;

    //Step 1: Registering the Window Class
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
    wc.lpszClassName = m_szClassName;
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassEx(&wc))
    {
        printf("Window Registration Failed!\n");
    }

    // Step 2: Creating the Window
    m_hWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        m_szClassName,
        NULL,//"The title of my window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, NULL, NULL);

    if(m_hWnd == NULL)
    {
        printf("Window Creation Failed!\n");
    }

	Attach(m_hWnd, (CNotifier*)this);

	ShowWindow(m_hWnd, SW_HIDE);
    UpdateWindow(m_hWnd);
	SendMessage(m_hWnd, WM_REGISTER_NOTIFIER, 0, 0);
	return TRUE;	
}

// Step 4: the Window Procedure
LRESULT CALLBACK CNotifier::GlobalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

	switch(msg)
	{
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		NotifierMap::iterator it = m_map.find(hWnd);
		if(it != m_map.end())
		{
			CNotifier* pNotifier = it->second;
			return pNotifier->InstanceProc(msg, wParam, lParam);
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

CWndInterfaceNotifier::CWndInterfaceNotifier(GUID guid)
{
	m_hWnd = NULL;
	m_guid = guid;
}

CWndInterfaceNotifier::~CWndInterfaceNotifier()
{
	SendMessage(m_hWnd, WM_DESTROY, 0, 0);
	Detach(m_hWnd);
	m_hWnd = NULL;
}
/*
BOOL CWndInterfaceNotifier::Create()
{
    WNDCLASSEX wc;

    //Step 1: Registering the Window Class
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
    wc.lpszClassName = m_szClassName;
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassEx(&wc))
    {
        printf("Window Registration Failed!\n");
    }

    // Step 2: Creating the Window
    m_hWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        m_szClassName,
        NULL,//"The title of my window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, NULL, NULL);

    if(m_hWnd == NULL)
    {
        printf("Window Creation Failed!\n");
    }

	Attach(m_hWnd, (CNotifier*)this);

	ShowWindow(m_hWnd, SW_HIDE);
    UpdateWindow(m_hWnd);
	SendMessage(m_hWnd, WM_REGISTER_NOTIFIER, 0, 0);
	return TRUE;	
}
*/
LRESULT CWndInterfaceNotifier::InstanceProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_REGISTER_NOTIFIER:
		if (!DoRegisterDeviceInterface(m_guid, m_hWnd))
		{
			printf("DoRegisterDeviceInterface Failed!\n");
		}
		break;
	case WM_CLOSE:
		if (!UnregisterDeviceNotification(m_hDeviceNotify)) {
			printf("UnregisterDeviceNotification Failed!\n");
		}
		break;
	case WM_DESTROY:
		break;
	case WM_DEVICECHANGE:
		DeviceChange(wParam, lParam);
		break;
	}
	return 0;
}

BOOL CWndInterfaceNotifier::DoRegisterDeviceInterface( 
    GUID InterfaceClassGuid, 
	HWND hWnd
)
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
    NotificationFilter.dbcc_size = 
        sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    m_hDeviceNotify = RegisterDeviceNotification( hWnd, 
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if(m_hDeviceNotify == NULL) 
    {
        printf("RegisterDeviceNotification failed: %d\n", 
                GetLastError());
        return FALSE;
    }
    return TRUE;
}

LRESULT CWndInterfaceNotifier::DeviceChange(WPARAM wParam, LPARAM lParam)
{
    // for more information, see MSDN help of WM_DEVICECHANGE
    // this part should not be very difficult to understand
	switch (wParam) {
		case DBT_DEVICEARRIVAL: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEARRIVAL, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
			if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				printf("DEV_BROADCAST_DEVICEINTERFACE, dbch_size = %d, dbch_devicetype = %x, dbcc_name = %s\n", 
					pDevInf->dbcc_size, pDevInf->dbcc_devicetype, pDevInf->dbcc_name);
			}
		}
		break;
		case DBT_DEVICEQUERYREMOVE: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEQUERYREMOVE, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
		}
		break;
		case DBT_DEVICEQUERYREMOVEFAILED: {
			printf("DBT_DEVICEQUERYREMOVEFAILED\n");
		}
		break;
		case DBT_DEVICEREMOVEPENDING: {
			printf("DBT_DEVICEREMOVEPENDING\n");
		}
		break;
		case DBT_DEVICEREMOVECOMPLETE: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEREMOVECOMPLETE, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
			if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				printf("DEV_BROADCAST_DEVICEINTERFACE, dbch_size = %d, dbch_devicetype = %x, dbcc_name = %s\n", 
					pDevInf->dbcc_size, pDevInf->dbcc_devicetype, pDevInf->dbcc_name);
			}
		}
		break;
		case DBT_DEVICETYPESPECIFIC: {
			printf("DBT_DEVICETYPESPECIFIC\n");
		}
		break;
		case DBT_CUSTOMEVENT: {
			PDEV_BROADCAST_HANDLE pHdr;
			pHdr = (PDEV_BROADCAST_HANDLE)lParam;
			printf("DBT_CUSTOMEVENT\n");
			if (memcmp(&pHdr->dbch_eventguid,
                   &GUID_VIOSERIAL_PORT_CHANGE_STATUS,
                   sizeof(GUID)) == 0) 
			{
				PVIRTIO_PORT_STATUS_CHANGE pEventInfo = (PVIRTIO_PORT_STATUS_CHANGE) pHdr->dbch_data;
				printf("Version = %d, Reason = %d\n", pEventInfo->Version, pEventInfo->Reason);
			}

		}
		break;
	}
	return 0;
}


CWndHandlerNotifier::CWndHandlerNotifier(HANDLE hndl)
{
	m_hWnd = NULL;
	m_hndl = hndl;
}

CWndHandlerNotifier::~CWndHandlerNotifier()
{
	SendMessage(m_hWnd, WM_DESTROY, 0, 0);
	Detach(m_hWnd);
	m_hndl = NULL;
}

/*
BOOL CWndHandlerNotifier::Create()
{
    WNDCLASSEX wc;

    //Step 1: Registering the Window Class
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
    wc.lpszClassName = m_szClassName;
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassEx(&wc))
    {
        printf("Window Registration Failed!\n");
    }

    // Step 2: Creating the Window
    m_hWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        m_szClassName,
        NULL,//"The title of my window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, NULL, NULL);

    if(m_hWnd == NULL)
    {
        printf("Window Creation Failed!\n");
    }

	Attach(m_hWnd, (CNotifier*)this);

	ShowWindow(m_hWnd, SW_HIDE);
    UpdateWindow(m_hWnd);
	SendMessage(m_hWnd, WM_REGISTER_NOTIFIER, 0, 0);
	return TRUE;	
}
*/

LRESULT CWndHandlerNotifier::InstanceProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_REGISTER_NOTIFIER:
		if (!DoRegisterDeviceHandler(m_hndl, m_hWnd))
		{
			printf("DoRegisterDeviceInterface Failed!\n");
		}
		break;
	case WM_CLOSE:
		if (!UnregisterDeviceNotification(m_hDeviceNotify)) {
			printf("UnregisterDeviceNotification Failed!\n");
		}
		break;
	case WM_DESTROY:
		break;
	case WM_DEVICECHANGE:
		DeviceChange(wParam, lParam);
		break;
	}
	return 0;
}

BOOL CWndHandlerNotifier::DoRegisterDeviceHandler( 
    HANDLE  Handle, 
	HWND hWnd
)
{
    DEV_BROADCAST_HANDLE NotificationFilter;

    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
    NotificationFilter.dbch_size = 
        sizeof(DEV_BROADCAST_HANDLE);
    NotificationFilter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    NotificationFilter.dbch_handle = Handle;
    NotificationFilter.dbch_eventguid = GUID_VIOSERIAL_PORT_CHANGE_STATUS;
	

    m_hDeviceNotify = RegisterDeviceNotification( hWnd, 
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if(m_hDeviceNotify == NULL) 
    {
        printf("RegisterDeviceNotification failed: %d\n", 
                GetLastError());
        return FALSE;
    }

    return TRUE;
}

LRESULT CWndHandlerNotifier::DeviceChange(WPARAM wParam, LPARAM lParam)
{
    // for more information, see MSDN help of WM_DEVICECHANGE
    // this part should not be very difficult to understand
	switch (wParam) {
		case DBT_DEVICEARRIVAL: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEARRIVAL, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
			if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				printf("DEV_BROADCAST_DEVICEINTERFACE, dbch_size = %d, dbch_devicetype = %x, dbcc_name = %s\n", 
					pDevInf->dbcc_size, pDevInf->dbcc_devicetype, pDevInf->dbcc_name);
			}
		}
		break;
		case DBT_DEVICEQUERYREMOVE: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEQUERYREMOVE, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
		}
		break;
		case DBT_DEVICEQUERYREMOVEFAILED: {
			printf("DBT_DEVICEQUERYREMOVEFAILED\n");
		}
		break;
		case DBT_DEVICEREMOVEPENDING: {
			printf("DBT_DEVICEREMOVEPENDING\n");
		}
		break;
		case DBT_DEVICEREMOVECOMPLETE: {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			printf("DBT_DEVICEREMOVECOMPLETE, dbch_size = %d, dbch_devicetype = %x\n", 
				pHdr->dbch_size, pHdr->dbch_devicetype);
			if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				printf("DEV_BROADCAST_DEVICEINTERFACE, dbch_size = %d, dbch_devicetype = %x, dbcc_name = %s\n", 
					pDevInf->dbcc_size, pDevInf->dbcc_devicetype, pDevInf->dbcc_name);
			}
		}
		break;
		case DBT_DEVICETYPESPECIFIC: {
			printf("DBT_DEVICETYPESPECIFIC\n");
		}
		break;
		case DBT_CUSTOMEVENT: {
			PDEV_BROADCAST_HANDLE pHdr;
			pHdr = (PDEV_BROADCAST_HANDLE)lParam;
			printf("DBT_CUSTOMEVENT\n");
			if (memcmp(&pHdr->dbch_eventguid,
                   &GUID_VIOSERIAL_PORT_CHANGE_STATUS,
                   sizeof(GUID)) == 0) 
			{
				PVIRTIO_PORT_STATUS_CHANGE pEventInfo = (PVIRTIO_PORT_STATUS_CHANGE) pHdr->dbch_data;
				printf("Version = %d, Reason = %d\n", pEventInfo->Version, pEventInfo->Reason);
			}

		}
		break;
	}
	return 0;
}

