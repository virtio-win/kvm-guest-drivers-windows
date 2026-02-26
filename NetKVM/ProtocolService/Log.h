#pragma once

extern FILE *LogFile;

#define ELEMENTS_IN(a) sizeof(a) / sizeof(a[0])
#define Log(fmt, ...)                                                                                                  \
    {                                                                                                                  \
        CStringA _s_;                                                                                                  \
        _s_.Format(fmt "\n", __VA_ARGS__);                                                                             \
        if (!LogFile) { OutputDebugStringA(_s_); } else { fwrite(_s_.GetString(), 1, _s_.GetLength(), LogFile); }      \
    }
