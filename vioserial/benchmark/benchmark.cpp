#include "stdafx.h"

template<class IOPROVIDER>
BOOL RunBenchmarkWorker(HANDLE hPort, SIZE_T cbRequestSize, DWORD dwConcurency, DWORD dwIterations)
{
    std::unique_ptr<OVERLAPPED[]> lpOverlapped(new OVERLAPPED[dwConcurency]);
    std::unique_ptr<HANDLE[]> lpHandles(new HANDLE[dwConcurency + 1]);
    HANDLE* hTimer = &lpHandles[0];
    HANDLE* lpEvents = &lpHandles[1];
    std::unique_ptr<BYTE[]> lpBuffer(new BYTE[cbRequestSize]);
    memset(lpBuffer.get(), 0, cbRequestSize);

    // set up the timer
    *hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
    if (*hTimer == NULL)
    {
        wprintf(L"CreateWaitableTimer failed with error %d\n", GetLastError());
        return FALSE;
    }

    SIZE_T cbTotalTransferred = 0;
    LARGE_INTEGER iDueTime = { 0, 0 };
    if (!SetWaitableTimer(*hTimer, &iDueTime, 1000, NULL, NULL, FALSE))
    {
        wprintf(L"SetWaitableTimer failed with error %d\n", GetLastError());
        CloseHandle(*hTimer);
        return FALSE;
    }

    // prepare requests
    for (DWORD i = 0; i < dwConcurency; i++)
    {
        memset(&lpOverlapped[i], 0, sizeof(OVERLAPPED));
        lpEvents[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (lpEvents[i] == NULL)
        {
            wprintf(L"CreateEvent failed with error %d\n", GetLastError());
            while (i-- > 0)
            {
                CloseHandle(lpEvents[i]);
            }
            CloseHandle(*hTimer);
            return FALSE;
        }
    }

    // main loop
    while (!_kbhit() && dwIterations > 0)
    {
        // (re)initialize all overlapped structures and start the I/O
        for (DWORD i = 0; i < dwConcurency; i++)
        {
            if (lpOverlapped[i].hEvent == NULL)
            {
                lpOverlapped[i].hEvent = lpEvents[i];
                if (!IOPROVIDER::StartIO(hPort, lpBuffer.get(), (DWORD)cbRequestSize, &lpOverlapped[i]))
                {
                    DWORD err = GetLastError();
                    if (err != ERROR_IO_PENDING)
                    {
                        wprintf(L"StartIO failed with error %d\n", err);
                        memset(&lpOverlapped[i], 0, sizeof(OVERLAPPED));
                    }
                }
            }
        }

        // wait for any of the overlapped events to get signaled or for a timer tick
        DWORD dwWaitRes = WaitForMultipleObjects(dwConcurency + 1, lpHandles.get(), FALSE, INFINITE);
        if (dwWaitRes == WAIT_OBJECT_0)
        {
            // the timer object was signaled
            if (cbTotalTransferred != 0)
            {
                wprintf(L"Parallelism %d, throughput %Iu\n", dwConcurency, cbTotalTransferred);
                cbTotalTransferred = 0;
                dwIterations--;
            }
        }
        else if (dwWaitRes > WAIT_OBJECT_0 && dwWaitRes <= WAIT_OBJECT_0 + dwConcurency)
        {
            // one of the overlapped events was signaled
            for (DWORD idx = dwWaitRes - WAIT_OBJECT_0 - 1; idx < dwWaitRes; idx++)
            {
                if (WaitForSingleObject(lpEvents[idx], 0) == WAIT_OBJECT_0)
                {
                    ResetEvent(lpEvents[idx]);
                    DWORD cbTransferred;
                    if (IOPROVIDER::CompleteIO(hPort, (DWORD)cbRequestSize, &lpOverlapped[idx], &cbTransferred))
                    {
                        cbTotalTransferred += cbTransferred;
                    }
                    else
                    {
                        DWORD err = GetLastError();
                        wprintf(L"CompleteIO failed with error %d\n", err);
                    }
                    memset(&lpOverlapped[idx], 0, sizeof(OVERLAPPED));
                }
            }
        }
        else if (dwWaitRes == WAIT_FAILED)
        {
            wprintf(L"WaitForMultipleObjects failed with error %d\n", GetLastError());
        }
        else
        {
            wprintf(L"Unexpected WaitForMultipleObjects return value %d\n", dwWaitRes);
        }
    }

    // cancel all requests
    for (DWORD i = 0; i < dwConcurency; i++)
    {
        if (lpOverlapped[i].hEvent != NULL)
        {
            if (CancelIoEx(hPort, &lpOverlapped[i]) || GetLastError() != ERROR_NOT_FOUND)
            {
                DWORD cbTransferred;
                GetOverlappedResult(hPort, &lpOverlapped[i], &cbTransferred, TRUE);
            }
        }
    }

    // close all handles
    for (DWORD i = 0; i < dwConcurency; i++)
    {
        CloseHandle(lpEvents[i]);
    }
    CloseHandle(*hTimer);
    return TRUE;
}

BOOL RunWriteBenchmark(HANDLE hPort, SIZE_T cbRequestSize, DWORD dwConcurrency, DWORD dwIterations)
{
    class WriteIOProvider
    {
    public:
        static BOOL StartIO(HANDLE handle, LPVOID lpBuffer, DWORD cbBuffer, LPOVERLAPPED lpOverlapped)
        {
            return WriteFile(handle, lpBuffer, cbBuffer, NULL, lpOverlapped);
        }

        static BOOL CompleteIO(HANDLE handle, DWORD cbBuffer, LPOVERLAPPED lpOverlapped, LPDWORD lpcbTransferred)
        {
            // we expect to have written the entire buffer
            if (GetOverlappedResult(handle, lpOverlapped, lpcbTransferred, FALSE))
            {
                if (*lpcbTransferred != cbBuffer)
                {
                    wprintf(L"Written %d bytes which is not equal to request size %d",
                        *lpcbTransferred,
                        cbBuffer
                        );
                }
                return TRUE;
            }
            return FALSE;
        }
    };

    wprintf(
        L"Writing %Iu byte buffers, %u parallel requests, for %u seconds\n",
        cbRequestSize,
        dwConcurrency,
        dwIterations
        );
    return RunBenchmarkWorker<WriteIOProvider>(hPort, cbRequestSize, dwConcurrency, dwIterations);
}

BOOL RunReadBenchmark(HANDLE hPort, SIZE_T cbRequestSize, DWORD dwConcurrency, DWORD dwIterations)
{
    class ReadIOProvider
    {
    public:
        static BOOL StartIO(HANDLE hPort, LPVOID lpBuffer, DWORD cbBuffer, LPOVERLAPPED lpOverlapped)
        {
            return ReadFile(hPort, lpBuffer, cbBuffer, NULL, lpOverlapped);
        }

        static BOOL CompleteIO(HANDLE hPort, DWORD cbBuffer, LPOVERLAPPED lpOverlapped, LPDWORD lpcbTransferred)
        {
            // *lpcbTransferred may be less than cbBuffer which is fine for reads
            return GetOverlappedResult(hPort, lpOverlapped, lpcbTransferred, FALSE);
        }
    };

    wprintf(
        L"Reading into %Iu byte buffers, %u parallel requests, for %u seconds\n",
        cbRequestSize,
        dwConcurrency,
        dwIterations
        );
    return RunBenchmarkWorker<ReadIOProvider>(hPort, cbRequestSize, dwConcurrency, dwIterations);
}

BOOL RunBenchmark(
    LPCWSTR wszPortName,
    BenchmarkType type,
    SIZE_T cbRequestSize,
    DWORD dwConcurrency,
    DWORD dwIterations
    )
{
    WCHAR wszNameBuffer[MAX_PATH];
    swprintf(wszNameBuffer, _countof(wszNameBuffer), L"\\\\.\\%s", wszPortName);

    HANDLE hPort = CreateFile(
        wszNameBuffer,
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);
    if (hPort == INVALID_HANDLE_VALUE)
    {
        wprintf(L"Error opening port %s\n", wszPortName);
        return FALSE;
    }

    BOOL bResult = FALSE;
    BOOL bSingleRun = (dwConcurrency != 0);
    dwConcurrency = std::max((DWORD)1, dwConcurrency);
    do
    {
        switch (type)
        {
        case ReadBenchmark:
            bResult = RunReadBenchmark(hPort, cbRequestSize, dwConcurrency, dwIterations);
            break;
        case WriteBenchmark:
            bResult = RunWriteBenchmark(hPort, cbRequestSize, dwConcurrency, dwIterations);
            break;
        default:
            wprintf(L"Unknown benchmark type\n");
            break;
        }
        dwConcurrency++;
    } while (bResult && !bSingleRun && !_kbhit());

    while (_kbhit())
    {
        _getch();
    }

    CloseHandle(hPort);
    return bResult;
}
