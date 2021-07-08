#include "pch.h"
#include "Session.h"
#include "pipe.h"


CSession::CSession(ULONG Id) : m_PipeServer(NULL)
{
    PrintMessage(L"%ws Id = %d\n", __FUNCTIONW__, Id);

    ZeroMemory(&m_SessionInfo, sizeof(m_SessionInfo));
    m_SessionInfo.SessionId = Id;
    ZeroMemory(&m_ProcessInfo, sizeof(m_ProcessInfo));
}


CSession::~CSession(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    Close();
}

bool CSession::Init(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    std::wstring PipeName = PIPE_NAME + std::to_wstring(m_SessionInfo.SessionId);
    m_PipeServer = new PipeServer(PipeName);
    if (m_PipeServer && m_PipeServer->Init()) {
        std::wstring AppName = APP_NAME + std::to_wstring(m_SessionInfo.SessionId);
        return CreateProcessInSession(AppName);
    }

    PrintMessage(L"Failed to init Pipe Servern");
    return false;
}

void CSession::Close(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    if (m_PipeServer) {
        m_PipeServer->Close();
        delete m_PipeServer;
        m_PipeServer = NULL;
    }
}

void CSession::Pause(void)
{
}

void CSession::Resume(void)
{
}

bool CSession::CreateProcessInSession(const std::wstring & commandLine)
{
    HANDLE hToken = NULL;
    STARTUPINFO si = { sizeof(si) };
    LPVOID lpvEnv = NULL;
    DWORD dwError = ERROR_SUCCESS;
    wchar_t szUserProfileDir[MAX_PATH];
    DWORD cchUserProfileDir = ARRAYSIZE(szUserProfileDir);
    DWORD ExitCode;
    DWORD dwWaitResult;

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    do {
        if (WTSGetActiveConsoleSessionId() != m_SessionInfo.SessionId)
        {
            PrintMessage(L"WTSGetActiveConsoleSessionId() != m_SessionInfo.SessionId\n");
            break;
        }

        if (!WTSQueryUserToken(m_SessionInfo.SessionId, &hToken))
        {
            dwError = GetLastError();
            PrintMessage(L"WTSQueryUserToken failed with error 0x%x\n", dwError);
            break;
        }

        TOKEN_LINKED_TOKEN admin = {};
        HANDLE hAdmToken = 0;
        DWORD dw = 0;
        if (GetTokenInformation(hToken, (TOKEN_INFORMATION_CLASS)TokenLinkedToken, &admin, sizeof(TOKEN_LINKED_TOKEN), &dw))
        {
            hAdmToken = admin.LinkedToken;
        }
        else
        {
            DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hAdmToken);
        }

        if (!CreateEnvironmentBlock(&lpvEnv, hToken, TRUE))
        {
            dwError = GetLastError();
            PrintMessage(L"CreateEnvironmentBlock failed with error 0x%x\n", dwError);
            break;
        }

        if (!GetUserProfileDirectory(hToken, szUserProfileDir,
            &cchUserProfileDir))
        {
            dwError = GetLastError();
            PrintMessage(L"GetUserProfileDirectory failed with error 0x%x\n", dwError);
            break;
        }

        si.lpDesktop = TEXT("winsta0\\default");

        if (!CreateProcessAsUser(hAdmToken, NULL, const_cast<wchar_t *>(commandLine.c_str()), NULL, NULL, FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, lpvEnv, szUserProfileDir, &si, &m_ProcessInfo))
        {
            dwError = GetLastError();
            PrintMessage(L"CreateProcessAsUser failed with error 0x%x\n", dwError);
            break;
        }

        WaitForSingleObject(m_ProcessInfo.hProcess, INFINITE);
        GetExitCodeProcess(m_ProcessInfo.hProcess, &ExitCode);
        PrintMessage(L"Process finished with code 0x%x\n", ExitCode);

    } while (0);

    if (hToken)
    {
        CloseHandle(hToken);
        hToken = NULL;
    }

    if (lpvEnv)
    {
        DestroyEnvironmentBlock(lpvEnv);
        lpvEnv = NULL;
    }

    if (m_ProcessInfo.hProcess)
    {
        CloseHandle(m_ProcessInfo.hProcess);
        m_ProcessInfo.hProcess = NULL;
    }

    if (m_ProcessInfo.hThread)
    {
        CloseHandle(m_ProcessInfo.hThread);
        m_ProcessInfo.hThread = NULL;
    }

    if (dwError != ERROR_SUCCESS)
    {
        SetLastError(dwError);
        return false;
    }

    return true;
}

