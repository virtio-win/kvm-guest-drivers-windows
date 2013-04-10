#include "stdafx.h"
#include "Exception.h"

neTKVMException::neTKVMException()
{

}

neTKVMException::neTKVMException(LPCTSTR lpzMessage)
{
    if (lpzMessage)
    {
        SetMessage(tstring(lpzMessage));
    }
}

neTKVMException::neTKVMException(const tstring &Message)
{
    SetMessage(Message);
}

neTKVMException::neTKVMException(const neTKVMException &Other)
{
    SetMessage(Other.m_Message);
}

neTKVMException::~neTKVMException(void)
{

}

const char *neTKVMException::what() const
{
    return m_MBCSMessage.c_str();
}

LPCTSTR neTKVMException::twhat() const
{
    return m_Message.c_str();
}

void neTKVMException::SetMessage(const tstring &Message)
{
    m_Message     = Message;
    m_MBCSMessage = tstring2string(m_Message);
}

neTKVMNumErrorException::neTKVMNumErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode)
    : neTKVMException(lpzDescription), m_dwErrorCode(dwErrorCode)
{

}

neTKVMNumErrorException::neTKVMNumErrorException(const tstring &Description, DWORD dwErrorCode)
    : neTKVMException(Description), m_dwErrorCode(dwErrorCode)
{

}

neTKVMNumErrorException::neTKVMNumErrorException(const neTKVMNumErrorException& Other)
    : neTKVMException(Other)
{
    m_dwErrorCode = Other.m_dwErrorCode;
}

neTKVMCRTErrorException::neTKVMCRTErrorException(int nErrorCode)
    : neTKVMNumErrorException(GetErrorString(nErrorCode), (DWORD)nErrorCode)
{

}

neTKVMCRTErrorException::neTKVMCRTErrorException(LPCTSTR lpzDescription, int nErrorCode)
    : neTKVMNumErrorException(tstring(lpzDescription) + GetErrorString((DWORD)nErrorCode), (DWORD)nErrorCode)
{

}

neTKVMCRTErrorException::neTKVMCRTErrorException(const tstring &Description, int nErrorCode)
: neTKVMNumErrorException(Description + GetErrorString((DWORD)nErrorCode), (DWORD)nErrorCode)
{

}

neTKVMCRTErrorException::neTKVMCRTErrorException(const neTKVMCRTErrorException &Other)
    : neTKVMNumErrorException(Other)
{

}

tstring neTKVMCRTErrorException::GetErrorString(DWORD dwErrorCode)
{
#ifdef WIN32
    TCHAR tcaBuff[256];
    _tcserror_s(tcaBuff, TBUF_SIZEOF(tcaBuff), (int)dwErrorCode);
    return tcaBuff;
#else
    return string2tstring(strerror((int)dwErrorCode));
#endif
}

#ifdef WIN32
neTKVMW32ErrorException::neTKVMW32ErrorException(DWORD dwErrorCode)
    : neTKVMNumErrorException(GetErrorString(m_dwErrorCode), dwErrorCode)
{

}

neTKVMW32ErrorException::neTKVMW32ErrorException(LPCTSTR lpzDescription, DWORD dwErrorCode)
    : neTKVMNumErrorException(tstring(lpzDescription) + GetErrorString(dwErrorCode), dwErrorCode)
{

}

neTKVMW32ErrorException::neTKVMW32ErrorException(const tstring &Description, DWORD dwErrorCode)
    : neTKVMNumErrorException(Description + GetErrorString(dwErrorCode), dwErrorCode)
{

}

neTKVMW32ErrorException::neTKVMW32ErrorException(const neTKVMW32ErrorException &Other)
    : neTKVMNumErrorException(Other)
{

}

tstring neTKVMW32ErrorException::GetErrorString(DWORD dwErrorCode)
{
    LPVOID lpMsgBuf;
    DWORD  msgLen = ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS|
                                    FORMAT_MESSAGE_MAX_WIDTH_MASK,
                                    NULL,
                                    dwErrorCode,
                                    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                    (LPTSTR) &lpMsgBuf,
                                    0,
                                    NULL);
    if(msgLen == 0)
    {
        tstringstream strm;
        strm << TEXT("Failed to get error description for error code: 0x") << hex << dwErrorCode;
        return strm.str();
    }
    else
    {
        tstring strResult((LPCTSTR)lpMsgBuf, msgLen);
        ::LocalFree( lpMsgBuf );
        return strResult;
    }
}
#endif // WIN32
