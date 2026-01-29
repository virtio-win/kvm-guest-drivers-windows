#ifndef DEVICE_H
#define DEVICE_H

#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <vector>
#include "..\sys\public.h"

class CDevice
{
  public:
    CDevice();
    ~CDevice();
    BOOL Init(BOOL ovrl, UINT portId);
    BOOL Write(PVOID buf, size_t *size);
    BOOL WriteEx(PVOID buf, size_t *size);
    BOOL Read(PVOID buf, size_t *size);
    BOOL ReadEx(PVOID buf, size_t *size);
    BOOL GetInfo(PVOID buf, size_t *size);

  protected:
    HANDLE m_hDevice;
    PTCHAR GetDevicePath(UINT portId, IN LPGUID InterfaceGuid, std::vector<uint8_t> &devpathBuf);
};

#endif
