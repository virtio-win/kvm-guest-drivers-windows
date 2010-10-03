// netco.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "NetKVMCo.h"

#if DBG
#define DbgPrint(Text) OutputDebugString(TEXT("KVMNetCoinstaller: " Text "\n"))
#else
#define DbgPrint(Text)
#endif

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
      DbgPrint("DIF_FINISHINSTALL_ACTION");
      break;

    case DIF_REMOVE:
      DbgPrint("DIF_REMOVE");
      break;

// Additional notifications that could be used
    case DIF_ADDPROPERTYPAGE_ADVANCED:
      DbgPrint("DIF_ADDPROPERTYPAGE_ADVANCED");
      break;

    case DIF_ALLOW_INSTALL:
      DbgPrint("DIF_ALLOW_INSTALL");
      break;

    case DIF_INSTALLDEVICE:
      DbgPrint("DIF_INSTALLDEVICE");
      break;

    case DIF_DESTROYPRIVATEDATA:
      DbgPrint("DIF_DESTROYPRIVATEDATA");
      break;

    case DIF_DETECT:
      DbgPrint("DIF_DETECT");
      break;



    case DIF_INSTALLDEVICEFILES:
      DbgPrint("DIF_INSTALLDEVICEFILES");
      break;

    case DIF_INSTALLINTERFACES:
      DbgPrint("DIF_INSTALLINTERFACES");
      break;

    case DIF_NEWDEVICEWIZARD_FINISHINSTALL:
      DbgPrint("DIF_NEWDEVICEWIZARD_FINISHINSTALL");
      break;

    case DIF_NEWDEVICEWIZARD_POSTANALYZE:
      DbgPrint("DIF_NEWDEVICEWIZARD_POSTANALYZE");
      break;

    case DIF_NEWDEVICEWIZARD_PRESELECT:
      DbgPrint("DIF_NEWDEVICEWIZARD_PRESELECT");
      break;

    case DIF_NEWDEVICEWIZARD_SELECT:
      DbgPrint("DIF_NEWDEVICEWIZARD_SELECT");
      break;

    case DIF_POWERMESSAGEWAKE:
      DbgPrint("DIF_POWERMESSAGEWAKE");
      break;

    case DIF_PROPERTYCHANGE:
      DbgPrint("DIF_PROPERTYCHANGE");
      break;

    case DIF_REGISTER_COINSTALLERS:
      DbgPrint("DIF_REGISTER_COINSTALLERS");
      break;

    case DIF_REGISTERDEVICE:
      DbgPrint("DIF_REGISTERDEVICE");
      break;

    case DIF_SELECTBESTCOMPATDRV:
      DbgPrint("DIF_SELECTBESTCOMPATDRV");
      break;

    case DIF_SELECTDEVICE:
      DbgPrint("DIF_SELECTDEVICE");
      break;

    case DIF_TROUBLESHOOTER:
      DbgPrint("DIF_TROUBLESHOOTER");
      break;

    case DIF_UNREMOVE:
      DbgPrint("DIF_UNREMOVE");
      break;

    default:
      DbgPrint("Unknown coinstaller event");
      break;
  }

  return NO_ERROR;
}
