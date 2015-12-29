#include "StdAfx.h"

SerialPort::SerialPort(wstring LinkName, PnPControl* ptr)
{
    Name = LinkName;
    Notify = NULL;
    NotificationPair.first = NULL;
    NotificationPair.second = NULL;
    Reference = 0;
    Handle = CreateFile(Name.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    ULONG   ulReturnedLength = 0;
    BYTE buf[512];
    PVIRTIO_PORT_INFO inf = (PVIRTIO_PORT_INFO)buf;
    BOOL res = DeviceIoControl(
        Handle,
        IOCTL_GET_INFORMATION,
        NULL,
        0,
        buf,
        sizeof(buf),
        &ulReturnedLength,
        NULL);
    if (res == FALSE)
    {
        printf ("Error. DeviceIoControl failed %d.\n", GetLastError());
        return;
    }
    HostConnected = inf->HostConnected;
    string s = inf->Name;

    SymbolicName.resize(s.length(),L' ');
    copy(s.begin(), s.end(), SymbolicName.begin());

    Control = ptr;
    CloseHandle(Handle);
    Handle = INVALID_HANDLE_VALUE;
}
SerialPort::~SerialPort()
{
    ClosePort();
}
void SerialPort::AddRef() {
    InterlockedIncrement(&Reference);
}
void SerialPort::Release() {
    if (InterlockedDecrement(&Reference) == 0) {
      delete this;
    }
}
BOOL SerialPort::OpenPort()
{
    Handle = CreateFile(Name.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    ULONG   ulReturnedLength = 0;
    BYTE buf[512];
    PVIRTIO_PORT_INFO inf = (PVIRTIO_PORT_INFO)buf;
    BOOL res = DeviceIoControl(
        Handle,
        IOCTL_GET_INFORMATION,
        NULL,
        0,
        buf,
        sizeof(buf),
        &ulReturnedLength,
        NULL);
    if (res == FALSE)
    {
        printf ("Error. DeviceIoControl failed %d.\n", GetLastError());
        return FALSE;
    }
    HostConnected = inf->HostConnected;
    Notify = Control->RegisterHandleNotify(Handle);
    return TRUE;
}
void SerialPort::ClosePort()
{
    if (Notify != NULL)
    {
        UnregisterDeviceNotification(Notify);
        Notify = NULL;
    }
    if (Handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Handle);
        Handle = INVALID_HANDLE_VALUE;
    }
}

BOOL SerialPort::ReadPort(PVOID buf, size_t *len)
{
    BOOL res = FALSE;
    DWORD ret;
    DWORD bytes = (DWORD)(*len);
    OVERLAPPED  ol = {0};

    if (buf == NULL || *len == 0)
    {
        *len = 0;
        return TRUE;
    }

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if( ol.hEvent == NULL)
    {
        return FALSE;
    }

    res = ReadFile ( Handle,
        buf,
        bytes,
        &ret,
        &ol
        );
    if (!res)
    {
        DWORD err = GetLastError();
        if ( err != ERROR_IO_PENDING)
        {
            printf("Read failed but isn't delayed. Error = %d\n", err);
            res = FALSE;
        }
        else
        {
            if (!GetOverlappedResult(Handle, &ol, &ret, TRUE))
            {
                res = FALSE;
            }
            else
            {
                *len = ret;
                res = TRUE;
            }
        }
    }
    else
    {
        *len = ret;
        res = TRUE;
    }

    CloseHandle( ol.hEvent );
    return res;
}

BOOL SerialPort::WritePort(PVOID buf, size_t *len)
{
    BOOL res = FALSE;
    ULONG ret = 0;
    DWORD bytes = (DWORD)(*len);
    OVERLAPPED  ol = {0};

    if (buf == NULL || *len == 0)
    {
        *len = 0;
        return TRUE;
    }

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if( ol.hEvent == NULL)
    {
        return FALSE;
    }

    res = WriteFile ( Handle,
        buf,
        bytes,
        &ret,
        &ol
        );
    if (!res)
    {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING)
        {
            printf("Write failed but isn't delayed. Error = %d\n", err);
            res = FALSE;
        }
        else
        {
            if (!GetOverlappedResult(Handle, &ol, &ret, TRUE))
            {
                res = FALSE;
            }
            else
            {
                *len = ret;
                res = TRUE;
            }
        }
    }
    else
    {
        *len = ret;
        res = TRUE;
    }

    CloseHandle( ol.hEvent );
    return res;
}


void SerialPort::handleEvent(const PnPControl& ref)
{
    PnPNotification Notification = ref.GetNotification();
    switch (Notification.wParam)
    {
    case DBT_CUSTOMEVENT: {
        PDEV_BROADCAST_HANDLE pHdr;
        pHdr = (PDEV_BROADCAST_HANDLE)Notification.lParam;
        if (IsEqualGUID(GUID_VIOSERIAL_PORT_CHANGE_STATUS, pHdr->dbch_eventguid) &&
            (pHdr->dbch_handle == Handle))
        {
            PVIRTIO_PORT_STATUS_CHANGE pEventInfo = (PVIRTIO_PORT_STATUS_CHANGE) pHdr->dbch_data;
            HostConnected = pEventInfo->Reason;
            if (NotificationPair.first)
            {
                NotificationPair.first(NotificationPair.second);
            }
        }
                          }
                          break;
    }
}

