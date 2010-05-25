#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

BOOL InstallService();
BOOL UninstallService();
BOOL GetConfiguration();
BOOL ChangeConfig();
BOOL ServiceRun();
BOOL ServiceControl(int ctrl);

void ShowUsage();
void ErrorHandler(char *s, int err);
void PrintMessage(char *s);

#endif
