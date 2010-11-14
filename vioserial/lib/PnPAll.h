#pragma once

#include "stdafx.h"
#include <list>
#include <string>

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
public:
#pragma warning(push)
#pragma warning(disable: 4355)
    PnPControl() :  Thread(INVALID_HANDLE_VALUE), Notification(0, 0, 0)
    { 
        Init(); 
        FindControllers(); 
        FindPorts();
    }
	BOOL FindPort(const wchar_t* name);
	PVOID OpenPort(const wchar_t* name);
	BOOL ReadPort(PVOID port, PVOID buf, PULONG size);
	BOOL WritePort(PVOID port, PVOID buf, ULONG size);
	VOID ClosePort(PVOID port);
	size_t NumPorts() {return Ports.size();};
	wchar_t* PortSymbolicName(int index);
#pragma warning(pop)
    ~PnPControl() {Close();}
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
    HDEVNOTIFY RegisterInterfaceNotify(GUID InterfaceClassGuid);
    HANDLE Thread;
    HWND Wnd;
};

class SerialController : public IPnPEventObserver
{
private:
#pragma warning(push)
#pragma warning(disable: 4355)
    SerialController() { }
#pragma warning(pop)
public:
#pragma warning(push)
#pragma warning(disable: 4355)
    SerialController(wstring LinkName) {Name = LinkName;}
    virtual ~SerialController() 
    {
        printf("~SerialController.\n");
    }
#pragma warning(pop)
    virtual void handleEvent(const PnPControl& ref)
    {
        UNREFERENCED_PARAMETER(  ref );
    }
};

class SerialPort : public IPnPEventObserver
{
private:
#pragma warning(push)
#pragma warning(disable: 4355)
    SerialPort() { }
#pragma warning(pop)
    HANDLE Handle;
    BOOL HostConnected;
    BOOL GuestConnected;
    HDEVNOTIFY Notify;
    PnPControl* Control;
public:
#pragma warning(push)
#pragma warning(disable: 4355)
    SerialPort(wstring LinkName, PnPControl* ptr);
    virtual ~SerialPort();
    BOOL OpenPort();
    void ClosePort();
    BOOL ReadPort(PVOID buf, size_t *len);
    BOOL WritePort(PVOID buf, size_t *len);
#pragma warning(pop)
    virtual void handleEvent(const PnPControl& ref);
};

