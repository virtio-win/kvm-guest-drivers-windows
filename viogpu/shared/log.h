#pragma once
#include <windows.h>

__declspec(noreturn) void ErrorHandler(const char *s, int err);
void PrintMessage(LPCWSTR pFormat, ...);
