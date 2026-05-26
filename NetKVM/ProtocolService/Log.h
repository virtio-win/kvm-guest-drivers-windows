#pragma once

extern void (*LogFn)(LPCSTR);
#define Log(fmt, ...)                                                                                                  \
    {                                                                                                                  \
        CStringA _s_;                                                                                                  \
        _s_.Format(fmt "\n", __VA_ARGS__);                                                                             \
        LogFn(_s_);                                                                                                    \
    }
