#pragma once

#define ELEMENTS_IN(a) sizeof(a)/sizeof(a[0])
#define Log(fmt, ...) { CStringA _s_; _s_.Format(fmt, __VA_ARGS__); OutputDebugStringA(_s_); }

