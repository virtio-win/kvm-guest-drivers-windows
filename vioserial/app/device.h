#ifndef DEVICE_H
#define DEVICE_H

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

class CDevice
{
public:
  CDevice ();
  ~CDevice ();
  BOOL Init (BOOL ovrl, UINT index);
  BOOL Write (PVOID buf, size_t *size);
  BOOL WriteEx (PVOID buf, size_t *size);
  BOOL Read (PVOID buf, size_t *size);
  BOOL ReadEx (PVOID buf, size_t *size);
  BOOL GetInfo (PVOID buf, size_t *size);

protected:
  HANDLE m_hDevice;
  PTCHAR GetDevicePath (UINT index, IN LPGUID InterfaceGuid);
};

#endif
