// netco.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "NetKVMCo.h"

NETCO_API DWORD CALLBACK NetCoinstaller (__in DI_FUNCTION InstallFunction,
                                         __in HDEVINFO DeviceInfoSet,
                                         __in PSP_DEVINFO_DATA DeviceInfoData,
                                         OPTIONAL __inout PCOINSTALLER_CONTEXT_DATA Context)
{
  UNREFERENCED_PARAMETER(Context);
  UNREFERENCED_PARAMETER(DeviceInfoData);
  UNREFERENCED_PARAMETER(DeviceInfoSet);

  switch (InstallFunction)
  {
    case DIF_FINISHINSTALL_ACTION:            // Install utility on this action
      OutputDebugString(TEXT("DIF_FINISHINSTALL_ACTION"));
      break;

    case DIF_REMOVE:
      OutputDebugString(TEXT("DIF_REMOVE"));
      break;

// Additional notifications that could be used
    case DIF_ADDPROPERTYPAGE_ADVANCED:
      OutputDebugString(TEXT("DIF_ADDPROPERTYPAGE_ADVANCED"));
      break;

    case DIF_ALLOW_INSTALL:
      OutputDebugString(TEXT("DIF_ALLOW_INSTALL"));
      break;

    case DIF_INSTALLDEVICE:
      OutputDebugString(TEXT("DIF_INSTALLDEVICE"));
      break;

    case DIF_DESTROYPRIVATEDATA:
      OutputDebugString(TEXT("DIF_DESTROYPRIVATEDATA"));
      break;

    case DIF_DETECT:
      OutputDebugString(TEXT("DIF_DETECT"));
      break;

    case DIF_INSTALLDEVICEFILES:
      OutputDebugString(TEXT("DIF_INSTALLDEVICEFILES"));
      break;

    case DIF_INSTALLINTERFACES:
      OutputDebugString(TEXT("DIF_INSTALLINTERFACES"));
      break;

    case DIF_NEWDEVICEWIZARD_FINISHINSTALL:
      OutputDebugString(TEXT("DIF_NEWDEVICEWIZARD_FINISHINSTALL"));
      break;

    case DIF_NEWDEVICEWIZARD_POSTANALYZE:
      OutputDebugString(TEXT("DIF_NEWDEVICEWIZARD_POSTANALYZE"));
      break;

    case DIF_NEWDEVICEWIZARD_PRESELECT:
      OutputDebugString(TEXT("DIF_NEWDEVICEWIZARD_PRESELECT"));
      break;

    case DIF_NEWDEVICEWIZARD_SELECT:
      OutputDebugString(TEXT("DIF_NEWDEVICEWIZARD_SELECT"));
      break;

    case DIF_POWERMESSAGEWAKE:
      OutputDebugString(TEXT("DIF_POWERMESSAGEWAKE"));
      break;

    case DIF_PROPERTYCHANGE:
      OutputDebugString(TEXT("DIF_PROPERTYCHANGE"));
      break;

    case DIF_REGISTER_COINSTALLERS:
      OutputDebugString(TEXT("DIF_REGISTER_COINSTALLERS"));
      break;

    case DIF_REGISTERDEVICE:
      OutputDebugString(TEXT("DIF_REGISTERDEVICE"));
      break;

    case DIF_SELECTBESTCOMPATDRV:
      OutputDebugString(TEXT("DIF_SELECTBESTCOMPATDRV"));
      break;

    case DIF_SELECTDEVICE:
      OutputDebugString(TEXT("DIF_SELECTDEVICE"));
      break;

    case DIF_TROUBLESHOOTER:
      OutputDebugString(TEXT("DIF_TROUBLESHOOTER"));
      break;

    case DIF_UNREMOVE:
      OutputDebugString(TEXT("DIF_UNREMOVE"));
      break;

    default:
      OutputDebugString(TEXT("Unknown coinstaller event"));
      break;
  }

  return NO_ERROR;
}
