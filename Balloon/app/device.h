#ifndef DEVICE_H
#define DEVICE_H

#define INITGUID

#include <windows.h>
#include <strsafe.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "public.h"


class CDevice {
public:
    CDevice();
    ~CDevice();
    BOOL     Init();
    BOOL     Write(PBALLOON_STAT pstat, int nr);
protected:
    HANDLE   m_hDevice;
    PTCHAR   GetDevicePath( IN  LPGUID InterfaceGuid );
};

#endif

