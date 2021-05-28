#pragma once
#include <windows.h>

#define ServiceName  TEXT("vgpusrv")
#define DisplayName  TEXT("VioGpu Resolution Service")

BOOL InstallService();
BOOL UninstallService();
BOOL GetConfiguration();
BOOL ChangeConfig();
BOOL ServiceRun();
BOOL ServiceControl(int ctrl);

void ShowUsage();
