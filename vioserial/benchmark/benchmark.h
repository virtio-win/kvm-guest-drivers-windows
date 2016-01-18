#pragma once

enum BenchmarkType
{
    ReadBenchmark,
    WriteBenchmark,
};

BOOL RunBenchmark(
    LPCWSTR wszPortName,   // for example "com.redhat.port1"
    BenchmarkType type,    // the type of benchmark to run
    SIZE_T cbRequestSize,  // size of each request in bytes
    DWORD dwConcurrency,   // number of requests running in parallel, 0 for a sweep
    DWORD dwIterations     // number of seconds to run the benchmark for
    );
