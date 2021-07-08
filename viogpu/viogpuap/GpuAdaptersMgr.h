#pragma once

class GpuAdapter;

class Notification
{
private:
#pragma warning( push )
#pragma warning(disable: 26495)
    Notification() { };
#pragma warning( pop )
public:
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
    Notification(UINT msg, WPARAM wParam, LPARAM lParam) :
        msg(msg), wParam(wParam), lParam(lParam) { };
};

class GpuAdaptersMgr
{
public:
    GpuAdaptersMgr() : m_hAdapterNotify(NULL), m_hThread(NULL), m_hWnd(NULL) { }
    ~GpuAdaptersMgr() { }
    BOOL Init();
    void Close();
private:
    static void ProcessPnPNotification(GpuAdaptersMgr* ptr, Notification newNotification);
protected:
    void FindAdapters();
    BOOL FindDisplayDevice(PDISPLAY_DEVICE lpDisplayDevice,
        std::wstring& name,
        PDWORD adapterIndex);
    BOOL GetDisplayDevice(LPCTSTR lpDevice,
        DWORD iDevNum,
        PDISPLAY_DEVICE lpDisplayDevice,
        DWORD dwFlags);
    void AddAdapter(const wchar_t* name);
    void RemoveAllAdapters();
    void InvalidateAdapters();

    static DWORD WINAPI ServiceThread(GpuAdaptersMgr*);
    void Run();
    static LRESULT CALLBACK GlobalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HDEVNOTIFY RegisterInterfaceNotify(GUID InterfaceClassGuid);
private:
    std::list<GpuAdapter*> Adapters;
    typedef std::list<GpuAdapter*>::iterator Iterator;
protected:
    HDEVNOTIFY m_hAdapterNotify;
    HANDLE m_hThread;
    HWND m_hWnd;
};
