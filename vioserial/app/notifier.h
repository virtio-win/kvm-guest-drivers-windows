#ifndef NOTIFIER_H
#define NOTIFIER_H

#include "..\sys\public.h"
#include <basetyps.h>
#include <conio.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wtypes.h>
#pragma warning(disable : 4201)

#include <setupapi.h>
#include <winioctl.h>

#pragma warning(default : 4201)

class CNotifier
{
public:
  CNotifier ();
  ~CNotifier ();
  BOOL Init ();
  BOOL Stop ();

protected:
  static DWORD WINAPI ServiceThread (CNotifier *);
  void Run ();
  HANDLE m_hThread;
  HANDLE m_hEvent;
  HWND m_hWnd;
  BOOL m_bRunning;
};

#endif
