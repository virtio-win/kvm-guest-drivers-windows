#include <windows.h>
#include <string>
#include "log.h"
#include "strsafe.h"

void ErrorHandler(const char *s, int err)
{
    wprintf(L"Failed. Error %d\n", err );

    LPWSTR lpMsgBuf = NULL;
    if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)(&lpMsgBuf),
        0, NULL) > 0) {
        wprintf(L"%s\n", lpMsgBuf);
        PrintMessage(lpMsgBuf);
        LocalFree(lpMsgBuf);
    } else {
        wprintf(L"unknown error\n");
    }

    FILE* pLog;
    if (fopen_s(&pLog, "vgpusvp.log", "a") == 0 && pLog) {
        fprintf(pLog, "%s failed, error code = %d\n", s, err);
        fclose(pLog);
    }

    ExitProcess(err);
}

void PrintMessage(LPCWSTR pFormat, ...)
{
    va_list args;
    WCHAR debugOutputBuffer[1024] = {'0'};
    va_start(args, pFormat);
    StringCbVPrintf(debugOutputBuffer, sizeof(debugOutputBuffer), pFormat, args);
    va_end(args);

    OutputDebugString(debugOutputBuffer);

    FILE* pLog;
    if (fopen_s(&pLog, "vgpusvp.log", "a") == 0 && pLog) {
        fwprintf(pLog, debugOutputBuffer);
        fclose(pLog);
    }
}
