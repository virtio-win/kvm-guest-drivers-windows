#pragma once

#include "windows.h"
#include <winternl.h>
#include <string>

#define CONNECTED    L"\\CONNECTED"
#define CLOSE        L"\\CLOSE"
#define CLOSED       L"\\CLOSED"

#define PIPE_NAME    L"\\\\.\\pipe\\viogpupipe" 
#define APP_NAME     L"viogpuap.exe "
#define DATA_BUFFER_LENGTH 1024
#define DATA_BUFFER_SIZE   DATA_BUFFER_LENGTH * sizeof (wchar_t)

class PipeClient
{
public:
    PipeClient(std::wstring& sName);
    ~PipeClient(void);
    bool Init();
    void WaitRunning();
    void Close();
private:
    void SetData(std::wstring& sData);
    void GetData(std::wstring& sData);
    void ConnectToServer();
    static DWORD WINAPI ClientThread(LPVOID ptr);
    bool Read();
    bool Write();
    void Run();
private:
    const std::wstring m_sPipeName; // Pipe name
    HANDLE m_hPipe;                 // Pipe handle
    HANDLE m_hThread;               // Pipe thread
    wchar_t* m_buffer;              // Buffer to hold data
};

class PipeServer
{
public:
    PipeServer(const std::wstring& sName);
    ~PipeServer(void);
    bool Init();
    void Close();
private:
    static DWORD WINAPI ServerThread(LPVOID ptr);
    void Run();
    bool WaitForClient();
    void GetData(std::wstring& sData);
    void SetData(std::wstring& sData);
    bool Read();
    bool Write();
private:
    const std::wstring m_sPipeName;
    HANDLE m_hPipe;
    HANDLE m_hThread;
    wchar_t* m_buffer;
};
