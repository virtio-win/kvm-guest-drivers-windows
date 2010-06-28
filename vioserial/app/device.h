#ifndef DEVICE_H
#define DEVICE_H

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



class CDevice {
public:
    CDevice();
    ~CDevice();
    BOOL     Init(BOOL ovrl);
    BOOL     Write(PVOID buf, size_t* size);
    BOOL     WriteEx(PVOID buf, size_t* size);
    BOOL     Read(PVOID buf, size_t* size);
    BOOL     ReadEx(PVOID buf, size_t* size);
    BOOL     GetInfo(PVOID buf, size_t* size);
protected:
    HANDLE   m_hDevice;
    PTCHAR   GetDevicePath( IN  LPGUID InterfaceGuid );
};

#endif

