#include "pipe.h"
#include "log.h"

PipeServer::PipeServer(const std::wstring& sName) : m_sPipeName(sName),
                                                m_hPipe(INVALID_HANDLE_VALUE),
                                                m_hThread(NULL),
                                                m_buffer(NULL)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    m_buffer = new wchar_t[DATA_BUFFER_LENGTH];
}

PipeServer::~PipeServer(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    delete[] m_buffer;
    m_buffer = NULL;
}

void PipeServer::SetData(std::wstring& sData)
{
    PrintMessage(L"%ws data = %ws\n", __FUNCTIONW__, sData.c_str());
    memset(&m_buffer[0], 0, DATA_BUFFER_SIZE);
    wcsncpy_s(&m_buffer[0], DATA_BUFFER_LENGTH, sData.c_str(), __min(DATA_BUFFER_LENGTH, sData.size()));
}

void PipeServer::GetData(std::wstring& sData)
{
    sData.clear();
    sData.append(m_buffer);
    PrintMessage(L"%ws data = %ws\n", __FUNCTIONW__, sData.c_str());
}

bool PipeServer::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if(m_sPipeName.empty()) {
        PrintMessage(L"Error: Invalid pipe name\n");
        return false;
    }

    m_hPipe = ::CreateNamedPipe(
            m_sPipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            DATA_BUFFER_SIZE,
            DATA_BUFFER_SIZE, 
            NMPWAIT_USE_DEFAULT_WAIT,
            NULL);

    if(INVALID_HANDLE_VALUE == m_hPipe) {
        PrintMessage(L"CreateNamedPipe failed with error 0x%x\n", GetLastError());
        return false;
    }

    m_hThread = ::CreateThread(
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)ServerThread,
        (LPVOID)this,
        0,
        NULL);

    if (NULL == m_hThread) {
        PrintMessage(L"CreateThread failed with error 0x%x\n", GetLastError());
        return false;
    }
    return true;
}

void PipeServer::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    WaitForClient();
    while (Read()) {
        std::wstring command;
        GetData(command);
        if (!command.empty()) {
            
            if (!_wcsnicmp(CONNECTED, command.c_str(), command.size())) {

            } else if (!_wcsnicmp(CLOSED, command.c_str(), command.size())) {

            } else {

            }
       }
    }
}

DWORD WINAPI PipeServer::ServerThread(LPVOID ptr)
{
    PipeServer* pServer = reinterpret_cast<PipeServer*>(ptr);
    pServer->Run();
    return 0;
}

bool PipeServer::WaitForClient()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if(!ConnectNamedPipe(m_hPipe, NULL)) {
        if (ERROR_PIPE_CONNECTED != GetLastError()) {
            PrintMessage(L"GetLastError failed 0x%x\n", GetLastError());
            return false;
        }
    }
    return true;
}

void PipeServer::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ::FlushFileBuffers(m_hPipe);
    ::DisconnectNamedPipe(m_hPipe);
    ::CloseHandle(m_hPipe);
    m_hPipe = INVALID_HANDLE_VALUE;
    ::CloseHandle(m_hThread);
    m_hThread = NULL;
}

bool PipeServer::Read()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD drBytes = 0;
    BOOL bFinishedRead = FALSE;
    int read = 0;
    int left = DATA_BUFFER_SIZE * sizeof(wchar_t);
    do {

        bFinishedRead = ::ReadFile(
            m_hPipe,
            &m_buffer[read],
            left,
            &drBytes,
            NULL);

        if(!bFinishedRead && ERROR_MORE_DATA != GetLastError()) {
            bFinishedRead = FALSE;
            break;
        }

        read += drBytes;
        left -= drBytes;

    } while(!bFinishedRead);

    if(FALSE == bFinishedRead || 0 == drBytes)
    {
        PrintMessage(L"ReadFile failed\n");
        return false;
    }
    return true;
}

bool PipeServer::Write()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD dwBytes;
    BOOL bResult = ::WriteFile(
        m_hPipe,
        m_buffer,
        (DWORD)(::wcslen(m_buffer)*sizeof(wchar_t) + 1),
        &dwBytes,
        NULL);

    if(FALSE == bResult || wcslen(m_buffer)*sizeof(wchar_t) + 1 != dwBytes)
    {
        PrintMessage(L"WriteFile failed\n");
        return false;
    }
    return true;
}

PipeClient::PipeClient(std::wstring& sName) : m_sPipeName(sName),
                                                m_hPipe(INVALID_HANDLE_VALUE),
                                                m_hThread(NULL),
                                                m_buffer(NULL)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    m_buffer = new wchar_t[DATA_BUFFER_LENGTH];
}

PipeClient::~PipeClient(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    delete[] m_buffer;
    m_buffer = NULL;
}

void PipeClient::SetData(std::wstring& sData)
{
    PrintMessage(L"%ws data = %ws\n", __FUNCTIONW__, sData.c_str());
    memset(&m_buffer[0], 0, DATA_BUFFER_SIZE);
    wcsncpy_s(&m_buffer[0], DATA_BUFFER_LENGTH, sData.c_str(), __min(DATA_BUFFER_LENGTH, sData.size()));
}

void PipeClient::GetData(std::wstring& sData)
{
    sData.clear();
    sData.append(m_buffer);
    PrintMessage(L"%ws data = %ws\n", __FUNCTIONW__, sData.c_str());
}

bool PipeClient::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if(m_sPipeName.empty())
    {
        return false;
    }

    m_hThread = ::CreateThread(
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)ClientThread,
        (LPVOID)this,
        0,
        NULL);

    if (NULL == m_hThread)
    {
        PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
        return false;
    }
    return true;
}

DWORD WINAPI PipeClient::ClientThread(LPVOID ptr)
{
    PipeClient* pClient = reinterpret_cast<PipeClient*>(ptr);
    pClient->Run();
    return 0;
}

void PipeClient::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ConnectToServer();
    while (Read()) {
        std::wstring command;
        GetData(command);
        if (!command.empty()) {
            
            if (!_wcsnicmp(CLOSE, command.c_str(), command.size()))
            {
                std::wstring command(CLOSED);
                SetData(command);
                Write();
                return;
            } else
            {
                PrintMessage(L"Invalid command = %ws.\n", command.c_str());
            }
       }
    }
}

void PipeClient::WaitRunning()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (NULL != m_hThread) {
        WaitForSingleObject(m_hThread, INFINITE);
    }
}

void PipeClient::ConnectToServer()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_hPipe = ::CreateFile(
        m_sPipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if(INVALID_HANDLE_VALUE == m_hPipe)
    {
        PrintMessage(L"Could not connect to pipe server\n");
    }
    else
    {
        PrintMessage(L"Connected to the Pipe Server\n");
    }
}

void PipeClient::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ::FlushFileBuffers(m_hPipe);
    ::DisconnectNamedPipe(m_hPipe);
    ::CloseHandle(m_hPipe);
    m_hPipe = INVALID_HANDLE_VALUE;
    ::CloseHandle(m_hThread);
    m_hThread = NULL;
}

bool PipeClient::Read()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD drBytes = 0;
    BOOL bFinishedRead = FALSE;
    int read = 0;
    do
    {
        bFinishedRead = ::ReadFile(
            m_hPipe,
            &m_buffer[read],
            DATA_BUFFER_SIZE,
            &drBytes,
            NULL);

        if(!bFinishedRead && ERROR_MORE_DATA != GetLastError())
        {
            bFinishedRead = FALSE;
            break;
        }
        read += drBytes;

    }while(!bFinishedRead);

    if(FALSE == bFinishedRead || 0 == drBytes)
    {
        PrintMessage(L"ReadFile failed\n");
        return false;
    }
    return true;
}

bool PipeClient::Write()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD dwBytes;
    BOOL bResult = ::WriteFile(
        m_hPipe,
        m_buffer,
        (DWORD)(::wcslen(m_buffer)*sizeof(wchar_t) + 1),
        &dwBytes,
        NULL);

    if(FALSE == bResult || wcslen(m_buffer)*sizeof(wchar_t) + 1 != dwBytes)
    {
        PrintMessage(L"WriteFile failed\n");
        return false;
    }

    return true;
}
