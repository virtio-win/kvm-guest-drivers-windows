#ifndef NOTIFIER_H
#define NOTIFIER_H

#include "precomp.h"
#include <map>

class CNotifier;
typedef std::map<HWND, CNotifier*> NotifierMap;


class CNotifier {
public:
    CNotifier();
    virtual ~CNotifier();
    BOOL     Init();

protected:
    static DWORD WINAPI ServiceThread(CNotifier* );
    void Run();
	static LRESULT CALLBACK GlobalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	virtual LRESULT InstanceProc(UINT msg, WPARAM wParam, LPARAM lParam) = 0;
	virtual BOOL Create();
	void	Attach(HWND hWnd, CNotifier* pNotifier) {m_map[hWnd] = pNotifier;}
	void	Detach(HWND hWnd) {m_map.erase(m_map.find(hWnd));};
    HANDLE   m_hThread;
	HDEVNOTIFY m_hDeviceNotify;
	PWCHAR m_szClassName;
	static NotifierMap m_map;
	HWND m_hWnd;
};

class CWndInterfaceNotifier: public CNotifier{
public:
	CWndInterfaceNotifier(GUID guid);
	virtual ~CWndInterfaceNotifier();
	
	LRESULT InstanceProc(UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL DoRegisterDeviceInterface(GUID InterfaceClassGuid, HWND hWnd);
	LRESULT DeviceChange(WPARAM wParam, LPARAM lParam);
	
private:
	GUID	 m_guid;
};


class CWndHandlerNotifier: public CNotifier{
public:
	CWndHandlerNotifier(HANDLE hndl);
	virtual ~CWndHandlerNotifier();
	
	LRESULT InstanceProc(UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL DoRegisterDeviceHandler(HANDLE  Handle, HWND hWnd);
	LRESULT DeviceChange(WPARAM wParam, LPARAM lParam);
	
private:
	HANDLE m_hndl;
};


#endif

