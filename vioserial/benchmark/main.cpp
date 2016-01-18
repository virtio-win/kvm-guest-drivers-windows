#include "stdafx.h"

#define DEFAULT_REQUEST_SIZE 16384
#define DEFAULT_NUM_OF_ITERATIONS 5

VOID ShowUsage()
{
    wprintf(L"USAGE:\n");
    wprintf(L"benchmark <type> <port_name> [-s <request_size>] [-c <concurrency>] [-t <time>]\n");
    wprintf(L"\n");
    wprintf(L"<type>           (r)ead or (w)rite\n");
    wprintf(L"<port_name>      name of the port to use\n");
    wprintf(L"<request_size>   size of each I/O request in bytes, %u by default\n", DEFAULT_REQUEST_SIZE);
    wprintf(L"<concurrency>    number of requests to run in parallel, if ommitted\n");
    wprintf(L"                 benchmarks all concurrency levels 1 and up\n");
    wprintf(L"<time>           time in seconds to run for at each concurrency level,\n");
    wprintf(L"                 %u by default\n", DEFAULT_NUM_OF_ITERATIONS);
    wprintf(L"\n");
    wprintf(L"Example: benchmark w com.redhat.rhevm.vdsm1 -s 8192 -c 2 -t 10\n");
}

template<typename T>
BOOL ParseUInt(LPCWSTR wszStr, T* lpRes)
{
    LPWCH lpEnd;
    unsigned long long value = wcstoull(wszStr, &lpEnd, 10);
    if (*lpEnd != 0)
    {
        return FALSE;
    }
    if (value > std::numeric_limits<T>::max())
    {
        return FALSE;
    }
    *lpRes = (T)value;
    return TRUE;
}

ULONG _cdecl wmain(ULONG argc, PWCHAR argv[])
{
    if (argc < 3)
    {
        ShowUsage();
        return 1;
    }

    // parse the command line and execute the benchmark
    BenchmarkType type;
    switch (argv[1][0])
    {
    case 'r': type = ReadBenchmark; break;
    case 'w': type = WriteBenchmark; break;
    default:
        wprintf(L"Unrecognized benchmark type %s\n", argv[1]);
        return 1;
    }

    LPCWSTR wszPortName = argv[2];
    SIZE_T cbRequestSize = DEFAULT_REQUEST_SIZE;
    DWORD dwConcurrency = 0;
    DWORD dwIterations = DEFAULT_NUM_OF_ITERATIONS;

    for (ULONG i = 3; i < argc; i += 2)
    {
        LPCWSTR wszArg = argv[i];
        if (i + 1 >= argc)
        {
            wprintf(L"Missing option value after %s\n", wszArg);
            return 1;
        }

        if ((wszArg[0] == '-' || wszArg[0] == '/') && wszArg[1] != 0 && wszArg[2] == 0)
        {
            BOOL bSuccess = FALSE;
            switch (wszArg[1])
            {
            case 's': bSuccess = ParseUInt<SIZE_T>(argv[i + 1], &cbRequestSize); break;
            case 'c': bSuccess = ParseUInt<DWORD>(argv[i + 1], &dwConcurrency); break;
            case 't': bSuccess = ParseUInt<DWORD>(argv[i + 1], &dwIterations); break;
            default:
                wprintf(L"Unrecognized option %s\n", wszArg);
                return 1;
            }
            if (!bSuccess)
            {
                wprintf(L"Unrecognized number argument %s\n", argv[i + 1]);
                return 1;
            }
        }
        else
        {
            wprintf(L"Unrecognized option %s\n", wszArg);
            return 1;
        }
    }

    RunBenchmark(wszPortName, type, cbRequestSize, dwConcurrency, dwIterations);
    return 0;
}
