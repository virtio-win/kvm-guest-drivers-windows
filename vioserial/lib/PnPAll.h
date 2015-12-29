#pragma once

#include "stdafx.h"
#include <list>
#include <string>
#include "vioser.h"

using namespace std;

class PnPControl;

class IPnPEventObserver
{
public:
    virtual void handleEvent(const PnPControl&) = 0;
    virtual ~IPnPEventObserver() {}
    wstring Name;
    wstring SymbolicName;
};

class PnPNotification
{
private:
    PnPNotification(){ };
public:
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
    PnPNotification(UINT msg, WPARAM wParam, LPARAM lParam) :
    msg(msg), wParam(wParam), lParam(lParam){ };
};


class PnPControl
{
    list<IPnPEventObserver*> Ports;
    list<IPnPEventObserver*> Controllers;
    PnPNotification Notification;
    typedef list<IPnPEventObserver*>::iterator Iterator;
    void Notify()
    {
        for(Iterator it = Ports.begin(); it != Ports.end(); it++)
            (*it)->handleEvent(*this);
        for(Iterator it = Controllers.begin(); it != Controllers.end(); it++)
            (*it)->handleEvent(*this);
    }
    PnPControl() :  Thread(INVALID_HANDLE_VALUE), Notification(0, 0, 0),
      PortNotify(NULL), ControllerNotify(NULL)
    {
        IsRunningAsService();
        Init();
        FindControllers();
        FindPorts();
    }
    ~PnPControl() { Close(); }
public:
    static PnPControl* GetInstance()
    {
        if (PnPControl::Instance == NULL)
        {
           PnPControl::Instance = new PnPControl();
        }
        PnPControl::Reference++;
        return PnPControl::Instance;
    }
    static void CloseInstance()
    {
        PnPControl::Reference--;
        if ((PnPControl::Reference <= 0) &&
           (PnPControl::Instance != NULL))
        {
           delete Instance;
           Instance = NULL;
        }
    }
    BOOL FindPort(const wchar_t* name);
    PVOID OpenPortByName(const wchar_t* name);
    PVOID OpenPortById(UINT id);
    BOOL ReadPort(PVOID port, PVOID buf, PULONG size);
    BOOL WritePort(PVOID port, PVOID buf, ULONG size);
    VOID ClosePort(PVOID port);
    size_t NumPorts() {return Ports.size();};
    wchar_t* PortSymbolicName(int index);
    VOID RegisterNotification(PVOID port, VIOSERIALNOTIFYCALLBACK pfn, PVOID ptr);
    const PnPNotification& GetNotification() const
    {
        return Notification;
    }
    void DispatchPnpMessage(const PnPNotification &notification){
        Notification = notification;
        Notify();
    }
    HDEVNOTIFY RegisterHandleNotify(HANDLE handle);
private:
    static void ProcessPnPNotification(PnPControl* ptr, PnPNotification newNotification);
    static PnPControl* Instance;
    static int Reference;
protected:
    HDEVNOTIFY ControllerNotify;
    HDEVNOTIFY PortNotify;
    static DWORD WINAPI ServiceThread(PnPControl* );
    void Run();
    static LRESULT CALLBACK GlobalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Init();
    void Close();
    void FindControllers();
    void FindPorts();
    BOOL FindInstance(GUID guid, DWORD idx, wstring& name);
    void AddController(const wchar_t* name);
    void RemoveController(wchar_t* name);
    void AddPort(const wchar_t* name);
    void RemovePort(wchar_t* name);
    BOOL IsRunningAsService();
    HDEVNOTIFY RegisterInterfaceNotify(GUID InterfaceClassGuid);
    HANDLE Thread;
    HWND Wnd;
    CRITICAL_SECTION PortsCS;

};

class SerialController : public IPnPEventObserver
{
private:
    SerialController() { }
public:
    SerialController(wstring LinkName) {Name = LinkName;}
    virtual ~SerialController()
    {
        printf("~SerialController.\n");
    }
    virtual void handleEvent(const PnPControl& ref)
    {
        UNREFERENCED_PARAMETER(  ref );
    }
};

class SerialPort : public IPnPEventObserver
{
private:
    SerialPort() { }
    HANDLE Handle;
    BOOL HostConnected;
    BOOL GuestConnected;
    HDEVNOTIFY Notify;
    PnPControl* Control;
    UINT Reference;
public:
    SerialPort(wstring LinkName, PnPControl* ptr);
    virtual ~SerialPort();
    void AddRef();
    void Release();
    BOOL OpenPort();
    void ClosePort();
    BOOL ReadPort(PVOID buf, size_t *len);
    BOOL WritePort(PVOID buf, size_t *len);
    virtual void handleEvent(const PnPControl& ref);
    pair <VIOSERIALNOTIFYCALLBACK*, PVOID> NotificationPair;
};

