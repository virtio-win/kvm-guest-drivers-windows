
#include "..\..\build\vendor.ver"
#include "..\inc\install.h"
#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <windows.h>
#include <winsvc.h>

#define VIOSOCK_WSP_SERVICE L"VirtioSocketWSP"

static SERVICE_STATUS_HANDLE _statusHandle = NULL;
static SERVICE_STATUS _statusRecord;

static DWORD WINAPI _SvcHandlerEx(_In_ DWORD dwControl, _In_ DWORD dwEventType,
                                  _In_ LPVOID lpEventData,
                                  _In_ LPVOID lpContext) {
  DWORD ret = NO_ERROR;

  switch (dwControl) {
  case SERVICE_CONTROL_STOP:
    _statusRecord.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(_statusHandle, &_statusRecord);
    DeinstallProtocol();
    WSACleanup();
    _statusRecord.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(_statusHandle, &_statusRecord);
    break;
  case SERVICE_CONTROL_INTERROGATE:
    break;
  default:
    break;
  }

  return ret;
}

static void WINAPI _ServiceMain(_In_ DWORD dwArgc, _In_ LPTSTR *lpszArgv) {
  WSADATA wsaData = {0};
  DWORD err = ERROR_SUCCESS;

  memset(&_statusRecord, 0, sizeof(_statusRecord));
  _statusHandle =
      RegisterServiceCtrlHandlerExW(VIOSOCK_WSP_SERVICE, _SvcHandlerEx, NULL);
  if (_statusHandle != NULL) {
    _statusRecord.dwCurrentState = SERVICE_START_PENDING;
    _statusRecord.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SetServiceStatus(_statusHandle, &_statusRecord);
    err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err == ERROR_SUCCESS) {
      if (InstallProtocol() || WSAGetLastError() == WSANO_RECOVERY) {
        _statusRecord.dwCurrentState = SERVICE_RUNNING;
        _statusRecord.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE;
        SetServiceStatus(_statusHandle, &_statusRecord);
      } else
        err = GetLastError();

      if (err != ERROR_SUCCESS)
        WSACleanup();
    }

    if (err != ERROR_SUCCESS) {
      _statusRecord.dwCurrentState = SERVICE_STOPPED;
      SetServiceStatus(_statusHandle, &_statusRecord);
    }
  } else
    err = GetLastError();

  return;
}

int __cdecl main(int argc, char **argv) {
  int ret = ERROR_SUCCESS;
  SERVICE_TABLE_ENTRYW svcTable[2];

  memset(svcTable, 0, sizeof(svcTable));
  svcTable[0].lpServiceName = VIOSOCK_WSP_SERVICE;
  svcTable[0].lpServiceProc = _ServiceMain;
  if (!StartServiceCtrlDispatcherW(svcTable))
    ret = GetLastError();

  return ret;
}
