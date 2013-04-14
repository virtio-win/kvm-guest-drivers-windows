#pragma once

#include "tstrings.h"

using std::exception;

class neTKVMException : public exception
{
public:
    neTKVMException();
    neTKVMException(LPCTSTR lpzMessage);
    neTKVMException(const tstring &Message);
    neTKVMException(const neTKVMException &Other);
    virtual ~neTKVMException();

    virtual const char *what() const;
    virtual LPCTSTR     twhat() const;

protected:
    void SetMessage(const tstring &Message);

private:
    tstring m_Message;
    string  m_MBCSMessage;
};

class neTKVMNumErrorException : public neTKVMException
{
public:
    neTKVMNumErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode);
    neTKVMNumErrorException(const tstring &Description, DWORD dwErrorCode);
    neTKVMNumErrorException(const neTKVMNumErrorException& Other);

    DWORD GetErrorCode(void) { return m_dwErrorCode; }

protected:
    DWORD m_dwErrorCode;
};

class neTKVMCRTErrorException : public neTKVMNumErrorException
{
public:
    neTKVMCRTErrorException(int nErrorCode = errno);
    neTKVMCRTErrorException(LPCTSTR lpzDescription, int nErrorCode = errno);
    neTKVMCRTErrorException(const tstring &Description, int nErrorCode = errno);
    neTKVMCRTErrorException(const neTKVMCRTErrorException &Other);

protected:
    static tstring GetErrorString(DWORD dwErrorCode);
};

#ifdef WIN32
class neTKVMW32ErrorException : public neTKVMNumErrorException
{
public:
    neTKVMW32ErrorException(DWORD dwErrorCode = GetLastError());
    neTKVMW32ErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode = GetLastError());
    neTKVMW32ErrorException(const tstring &Description, DWORD dwErrorCode = GetLastError());
    neTKVMW32ErrorException(const neTKVMW32ErrorException &Other);

protected:
    static tstring GetErrorString(DWORD dwErrorCode);
};
#endif

