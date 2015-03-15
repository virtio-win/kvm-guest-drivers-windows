// RegAccess.cpp: implementation of the neTKVMRegAccess class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "RegAccess.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

neTKVMRegAccess::neTKVMRegAccess()
    : m_lpsRegPath(NULL), m_hkPrimaryHKey(0)
{
}

neTKVMRegAccess::neTKVMRegAccess(HKEY hNewPrKey, LPCTSTR lpzNewRegPath)
    : m_lpsRegPath(NULL), m_hkPrimaryHKey(0)
{
    SetPrimaryKey(hNewPrKey);
    if (SetRegPath(lpzNewRegPath) == FALSE)
    {
        throw neTKVMRegAccessConstructorFailedException();
    }
}

neTKVMRegAccess::~neTKVMRegAccess()
{
    delete m_lpsRegPath;
}

VOID neTKVMRegAccess::SetPrimaryKey(HKEY hNewPrKey)
{
    m_hkPrimaryHKey = hNewPrKey;
}

BOOL neTKVMRegAccess::SetRegPath(LPCTSTR lpzNewRegPath)
{
    delete m_lpsRegPath;

    if (!lpzNewRegPath)
    {
        m_lpsRegPath = NULL;
        return TRUE;
    }

    m_lpsRegPath = _tcsdup(lpzNewRegPath);
    return (m_lpsRegPath != NULL)?TRUE:FALSE;
}

HKEY neTKVMRegAccess::GetPrimaryKey(VOID)
{
    return m_hkPrimaryHKey;
}

BOOL neTKVMRegAccess::GetRegPath(LPTSTR lpsBuffer, DWORD dwNumberOfElements)
{
    if (!dwNumberOfElements)
    {
        return FALSE;
    }

    if (!m_lpsRegPath)
    {
        *lpsBuffer = 0;
        return TRUE;
    }

    return (_tcscpy_s(lpsBuffer, dwNumberOfElements, m_lpsRegPath) == 0)?TRUE:FALSE;
}

DWORD neTKVMRegAccess::ReadDWord(LPCTSTR lpzValueName,
                              DWORD   dwDefault,
                              LPCTSTR lpzSubKey)
{
    DWORD dwRes = 0;

    return (ReadDWord(lpzValueName, &dwRes, lpzSubKey) == TRUE)?dwRes:dwDefault;
}

BOOL neTKVMRegAccess::ReadDWord(LPCTSTR lpzValueName,
                             LPDWORD lpdwValue,
                             LPCTSTR lpzSubKey)
{
    BOOL  bRes = FALSE;
    DWORD dwValue = 0,
          dwSize = sizeof(dwValue),
          dwType = REG_DWORD;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkReadKeyHandle,
                            lpzValueName,
                            NULL,
                            &dwType,
                            (LPBYTE)&dwValue,
                            &dwSize) == ERROR_SUCCESS)
        {
            bRes = TRUE;
            if (lpdwValue)
            {
                *lpdwValue = dwValue;
            }
        }

        RegCloseKey(hkReadKeyHandle);
    }

    return bRes;
}

DWORD neTKVMRegAccess::ReadString(LPCTSTR lpzValueName,
                               LPTSTR  lpzData,
                               DWORD   dwNumberOfElements,
                               LPCTSTR lpzSubKey)
{
    DWORD dwRes = 0;
    DWORD dwType = REG_SZ;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwBuffSize = dwNumberOfElements * sizeof(lpzData[0]);

    memset(lpzData, 0, dwBuffSize);

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkReadKeyHandle,
                            lpzValueName,
                            NULL,
                            &dwType,
                            (LPBYTE)lpzData,
                            &dwBuffSize) == ERROR_SUCCESS)
            dwRes = dwBuffSize / sizeof(lpzData[0]);

        RegCloseKey(hkReadKeyHandle);
    }

    return dwRes;
}

DWORD neTKVMRegAccess::ReadBinary(LPCTSTR lpzValueName,
                               LPBYTE  lpzData,
                               DWORD   dwSize,
                               LPCTSTR lpzSubKey)
{
    DWORD dwRes = 0;
    DWORD dwType = REG_BINARY;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    memset(lpzData, 0, dwSize);

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkReadKeyHandle,
                            lpzValueName,
                            NULL,
                            &dwType,
                            lpzData,
                            &dwSize) == ERROR_SUCCESS)
            dwRes = dwSize;

        RegCloseKey(hkReadKeyHandle);
    }

    return dwRes;
}

BOOL neTKVMRegAccess::ReadValueName(LPTSTR  lpsValueName,
                                 DWORD   dwNumberOfElements,
                                 DWORD   dwIndex,
                                 LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    BYTE baData[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwDataSize = DEFAULT_REG_ENTRY_DATA_LEN,
          dwType = REG_BINARY;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        DWORD dwBuffSize = dwNumberOfElements * sizeof(lpsValueName[0]);
        if (RegEnumValue(hkReadKeyHandle,
                         dwIndex,
                         lpsValueName,
                         &dwBuffSize,
                         NULL,
                         &dwType,
                         baData,
                         &dwDataSize) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkReadKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::ReadKeyName(LPTSTR  lpsKeyName,
                               DWORD   dwNumberOfElements,
                               DWORD   dwIndex,
                               LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkReadKeyHandle = NULL;
    FILETIME stTimeFile;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_ENUMERATE_SUB_KEYS,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        DWORD dwBuffSize = dwNumberOfElements * sizeof(lpsKeyName[0]);
        if (RegEnumKeyEx(hkReadKeyHandle,
                         dwIndex,
                         lpsKeyName,
                         &dwBuffSize,
                         NULL,
                         NULL,
                         NULL,
                         &stTimeFile) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkReadKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::WriteValue(LPCTSTR lpzValueName,
                              DWORD   dwValue,
                              LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkWriteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwDisposition;

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegCreateKeyEx(m_hkPrimaryHKey,
                       tcaFullRegPath,
                       0,
                       TEXT(""),
                       REG_OPTION_NON_VOLATILE,
                       KEY_WRITE,
                       NULL,
                       &hkWriteKeyHandle,
                       &dwDisposition) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(hkWriteKeyHandle,
                          lpzValueName,
                          0,
                          REG_DWORD,
                          (LPCBYTE)&dwValue,
                          sizeof(DWORD)) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkWriteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::WriteString(LPCTSTR lpzValueName,
                               LPCTSTR lpzValue,
                               LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkWriteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwDisposition;

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegCreateKeyEx(m_hkPrimaryHKey,
                       tcaFullRegPath,
                       0,
                       TEXT(""),
                       REG_OPTION_NON_VOLATILE,
                       KEY_WRITE,
                       NULL,
                       &hkWriteKeyHandle,
                       &dwDisposition) == ERROR_SUCCESS)
    {
        DWORD dwBuffSize = ((DWORD)_tcslen(lpzValue) + 1) * sizeof(lpzValue[0]);
        if (RegSetValueEx(hkWriteKeyHandle,
                          lpzValueName,
                          0,
                          REG_SZ,
                          (LPCBYTE)lpzValue,
                          dwBuffSize) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkWriteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::WriteBinary(LPCTSTR lpzValueName,
                               LPCBYTE lpData,
                               DWORD   dwDataSize,
                               LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkWriteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwDisposition;

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegCreateKeyEx(m_hkPrimaryHKey,
                       tcaFullRegPath,
                       0,
                       TEXT(""),
                       REG_OPTION_NON_VOLATILE,
                       KEY_WRITE,
                       NULL,
                       &hkWriteKeyHandle,
                       &dwDisposition) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(hkWriteKeyHandle,
                          lpzValueName,
                          0,
                          REG_BINARY,
                          lpData,
                          dwDataSize) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkWriteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::AddKey(LPCTSTR lpzKeyName)
{
    BOOL bResult = FALSE;
    HKEY hkWriteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwDisposition;

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzKeyName);

    if (RegCreateKeyEx(m_hkPrimaryHKey,
                       tcaFullRegPath,
                       0,
                       TEXT(""),
                       REG_OPTION_NON_VOLATILE,
                       KEY_WRITE,
                       NULL,
                       &hkWriteKeyHandle,
                       &dwDisposition) == ERROR_SUCCESS)
    {
        bResult = TRUE;
        RegCloseKey(hkWriteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::DeleteKey(LPCTSTR lpzKeyName, LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkDeleteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_WRITE,
                     &hkDeleteKeyHandle) == ERROR_SUCCESS)
    {
        if (RegDeleteKey(hkDeleteKeyHandle,
                         lpzKeyName) == ERROR_SUCCESS)
            bResult = TRUE;

        RegCloseKey(hkDeleteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::DeleteValue(LPCTSTR lpzValueName, LPCTSTR lpzSubKey)
{
    BOOL bResult = FALSE;
    HKEY hkDeleteKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_WRITE,
                     &hkDeleteKeyHandle) == ERROR_SUCCESS)
    {
        if (RegDeleteValue(hkDeleteKeyHandle,
                           lpzValueName) == ERROR_SUCCESS)
            bResult = TRUE;
        RegCloseKey(hkDeleteKeyHandle);
    }

    return bResult;
}

BOOL neTKVMRegAccess::GetValueInfo(LPCTSTR lpzValueName,
                          DWORD* lpDataType,
                          DWORD* lpDataSize,
                          LPCTSTR lpzSubKey)
{
    BOOL bRet = FALSE;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkReadKeyHandle,
                            lpzValueName,
                            NULL,
                            lpDataType,
                            NULL,
                            lpDataSize) == ERROR_SUCCESS)
            bRet = TRUE;

        RegCloseKey(hkReadKeyHandle);
    }

    return bRet;
}

BOOL neTKVMRegAccess::GetKeyInfo(LPDWORD lpdwNofSubKeys,
                              LPDWORD lpdwMaxSubKeyLen,
                              LPDWORD lpdwNofValues,
                              LPDWORD lpdwMaxValueNameLen,
                              LPDWORD lpdwMaxValueLen,
                              LPCTSTR lpzSubKey)
{
    BOOL bRet = FALSE;
    HKEY hkReadKeyHandle = NULL;
    TCHAR tcaFullRegPath[DEFAULT_REG_ENTRY_DATA_LEN];

    FormatFullRegPath(tcaFullRegPath, TBUF_SIZEOF(tcaFullRegPath), lpzSubKey);

    if (RegOpenKeyEx(m_hkPrimaryHKey,
                     tcaFullRegPath,
                     0,
                     KEY_QUERY_VALUE,
                     &hkReadKeyHandle) == ERROR_SUCCESS)
    {
        if (RegQueryInfoKey(hkReadKeyHandle,
                            NULL,
                            NULL,
                            NULL,
                            lpdwNofSubKeys,
                            lpdwMaxSubKeyLen,
                            NULL,
                            lpdwNofValues,
                            lpdwMaxValueNameLen,
                            lpdwMaxValueLen,
                            NULL,
                            NULL) == ERROR_SUCCESS)
            bRet = TRUE;

        RegCloseKey(hkReadKeyHandle);
    }

    return bRet;

}


VOID neTKVMRegAccess::FormatFullRegPath(LPTSTR lpzFullPathBuff, DWORD_PTR dwNumberOfElements, LPCTSTR lpzSubKey)
{
    DWORD_PTR dwReqNumberOfElements = (m_lpsRegPath?_tcslen(m_lpsRegPath):0) +
                                      (lpzSubKey?_tcslen(lpzSubKey):0) +
                                      ((m_lpsRegPath && lpzSubKey)?1:0) + 1;

    memset(lpzFullPathBuff, 0, dwNumberOfElements);
    if (dwNumberOfElements >= dwReqNumberOfElements)
    {
        if (m_lpsRegPath)
            _tcscpy_s(lpzFullPathBuff, dwNumberOfElements, m_lpsRegPath);

        if (lpzSubKey)
        {
            if (m_lpsRegPath)
                _tcscat_s(lpzFullPathBuff, dwNumberOfElements, TEXT("\\"));
            _tcscat_s(lpzFullPathBuff, dwNumberOfElements, lpzSubKey);
        }
    }
}

