#pragma once

#pragma warning(push, 3)
#pragma warning(disable : 4995) // name was marked as #pragma deprecated
#include <exception>
#include <iomanip>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#pragma warning(pop)

using std::allocator;
using std::cout;
using std::list;
using std::string;
using std::stringstream;
using std::wcout;
using std::wstring;
using std::wstringstream;

string __wstring2string(const wstring &str);
wstring __string2wstring(const string &str);

#if defined(UNICODE)
typedef wstring tstring;
typedef wstringstream tstringstream;
#define tcout wcout
#define tstring2string(str) __wstring2string(str)
#define string2tstring(str) __string2wstring(str)
#define tstring2wstring(str) (str)
#define wstring2tstring(str) (str)
#else
typedef string tstring;
typedef stringstream tstringstream;
#define tcout cout
#define tstring2string(str) (str)
#define string2tstring(str) (str)
#define tstring2wstring(str) __string2wstring(str)
#define wstring2tstring(str) __wstring2string(str)
#endif

typedef list<tstring, allocator<tstring>> neTKVMTStrList;

#define TBUF_SIZEOF(a) ARRAY_SIZE(a)
