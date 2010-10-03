#ifndef NOTIFYER_H
#define NOTIFYER_H

#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include "..\sys\public.h"
#pragma warning(disable:4201)

#include <setupapi.h>
#include <winioctl.h>

#pragma warning(default:4201)

class CNotifier {
public:
    CNotifier();
    ~CNotifier();
    BOOL     Init();
    BOOL     Stop();
protected:
    static DWORD WINAPI ServiceThread(CNotifier* );
    void Run();
    HANDLE   m_hThread;
    HANDLE   m_hEvent;
    HWND     m_hWnd;
    BOOL     m_bRunning;
};

#endif

