// RegAccess.h: interface for the neTKVMRegAccess class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

const DWORD DEFAULT_REG_ENTRY_DATA_LEN  = 0x00000100;

class neTKVMRegAccessConstructorFailedException : public neTKVMException
{
public:
    neTKVMRegAccessConstructorFailedException() :
      neTKVMException(TEXT("Can't construct neTKVMRegAccess object"))
    { }
};

class neTKVMRegAccess
{
public:
    neTKVMRegAccess();
    neTKVMRegAccess(HKEY hNewPrKey, LPCTSTR lpzNewRegPath);
    virtual ~neTKVMRegAccess();

    VOID  SetPrimaryKey(HKEY hNewPrKey);
    BOOL  SetRegPath(LPCTSTR lpzNewRegPath);
    HKEY  GetPrimaryKey(VOID);
    BOOL  GetRegPath(LPTSTR lpsBuffer,
                     DWORD  dwNumberOfElements);

    BOOL  ReadValueName(LPTSTR lpsValueName,
                        DWORD  dwNumberOfElements,
                        DWORD  dwIndex=0,
                        LPCTSTR lpzSubKey=NULL);
    BOOL  ReadKeyName(LPTSTR  lpsKeyName,
                      DWORD   dwNumberOfElements,
                      DWORD   dwIndex,
                      LPCTSTR lpzSubKey = NULL);

    BOOL GetValueInfo(LPCTSTR lpzValueName,
                      DWORD*  lpDataType,
                      DWORD*  lpDataSize,
                      LPCTSTR lpzSubKey = NULL);
    BOOL GetKeyInfo(LPDWORD lpdwNofSubKeys,
                    LPDWORD lpdwMaxSubKeyLen,
                    LPDWORD lpdwNofValues,
                    LPDWORD lpdwMaxValueNameLen,
                    LPDWORD lpdwMaxValueLen,
                    LPCTSTR lpzSubKey = NULL);

    DWORD ReadDWord(LPCTSTR lpzValueName,
                    DWORD   dwDefault = 0,
                    LPCTSTR lpzSubKey = NULL);
    BOOL  ReadDWord(LPCTSTR lpzValueName,
                    LPDWORD lpdwValue,
                    LPCTSTR lpzSubKey = NULL);
    DWORD ReadString(LPCTSTR lpzValueName,
                     LPTSTR  lpzData,
                     DWORD   dwNumberOfElements,
                     LPCTSTR lpzSubKey=NULL);
    DWORD ReadBinary(LPCTSTR lpzValueName,
                     LPBYTE  lpzData,
                     DWORD   dwSize,
                     LPCTSTR lpzSubKey=NULL);
    BOOL  WriteValue(LPCTSTR lpzValueName,
                     DWORD  dwValue,
                     LPCTSTR lpzSubKey = NULL);
    BOOL  WriteString(LPCTSTR lpzValueName,
                      LPCTSTR lpzValue,
                      LPCTSTR lpzSubKey=NULL);
    BOOL  WriteBinary(LPCTSTR lpzValueName,
                      LPCBYTE lpData,
                      DWORD   dwDataSize,
                      LPCTSTR lpzSubKey = NULL);
    BOOL  DeleteKey(LPCTSTR lpzKeyName,
                    LPCTSTR lpzSubKey = NULL);
    BOOL  DeleteValue(LPCTSTR lpzValueName,
                      LPCTSTR lpzSubKey = NULL);

    BOOL  AddKey(LPCTSTR lpzKeyName);

protected:
    VOID FormatFullRegPath(LPTSTR    lpzFullPathBuff,
                           DWORD_PTR dwNumberOfElements,
                           LPCTSTR   lpzSubKey);

    LPTSTR m_lpsRegPath;
    HKEY   m_hkPrimaryHKey;
};
