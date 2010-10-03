#pragma once

#pragma warning(push, 3)
#pragma warning(disable:4995) //name was marked as #pragma deprecated
#include <string>
#include <iostream>
#include <iomanip>
#include <exception>
#include <sstream>
#include <list>
#pragma warning(pop)

using std::string;
using std::wstring;
using std::stringstream;
using std::wstringstream;
using std::wcout;
using std::cout;
using std::exception;
using std::list;
using std::allocator;

string __wstring2string(const wstring& str);
wstring __string2wstring(const string& str);

#if defined(UNICODE)
    typedef wstring tstring;
    typedef wstringstream tstringstream;
#   define tcout wcout
#   define tstring2string(str) __wstring2string(str)
#   define string2tstring(str) __string2wstring(str)
#   define tstring2wstring(str) (str)
#   define wstring2tstring(str) (str)
#else
    typedef string tstring;
    typedef stringstream tstringstream;
#   define tcout cout
#   define tstring2string(str) (str)
#   define string2tstring(str) (str)
#   define tstring2wstring(str) __string2wstring(str)
#   define wstring2tstring(str) __wstring2string(str)
#endif

class neTKVMException : public exception
{
public:
	neTKVMException(tstring Message) :
	  m_Message(Message) 
	{ 
		m_MBCSMessage = tstring2string(m_Message); 
	}
	virtual ~neTKVMException(void) {}

	virtual const char *what() const
	{
		return m_MBCSMessage.c_str();
	}

	virtual LPCTSTR twhat() const
	{
		return m_Message.c_str();
	}

protected:
	tstring m_Message;
	string  m_MBCSMessage;
};

typedef list<tstring, allocator<tstring>> neTKVMTStrList;

#define TBUF_SIZEOF(a) ARRAY_SIZE(a)
