#include "stdafx.h"

string __wstring2string(const wstring& str)
{
   size_t buf_length = str.length() + 1;
   char* buf = new (std::nothrow) char[buf_length];
   size_t nCount;

   if(NULL == buf)
   {
       return string();
   }

   if( 0 != wcstombs_s(&nCount, buf, buf_length, str.c_str(), buf_length) )
   {// Return empty string in case of failure
        buf[0] = 0;
   }

   string result(buf);
   delete [] buf;
   return result;
}

wstring __string2wstring(const string& str)
{
    size_t buf_length = str.length() + 1;
    wchar_t* buf = new (std::nothrow) wchar_t[buf_length];
    size_t nCount;

    if(NULL == buf)
    {
        return wstring();
    }

    if( 0 != mbstowcs_s(&nCount, buf, buf_length, str.c_str(), buf_length) )
    {// Return empty string in case of failure
        buf[0] = 0;
    }

    wstring result(buf);
    delete [] buf;
    return result;
}
