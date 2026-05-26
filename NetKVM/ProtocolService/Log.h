#pragma once

#define ELEMENTS_IN(a) sizeof(a) / sizeof(a[0])
extern void (*LogFn)(LPCSTR);
#define Log(fmt, ...)                                                                                                  \
    {                                                                                                                  \
        CStringA _s_;                                                                                                  \
        _s_.Format(fmt "\n", __VA_ARGS__);                                                                             \
        LogFn(_s_);                                                                                                    \
    }
