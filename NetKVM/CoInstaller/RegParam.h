#pragma once

#include "tstrings.h"
#include "RegAccess.h"

enum neTKVMRegParamType
{
    NETKVM_RTT_ENUM,
    NETKVM_RTT_INT,
    NETKVM_RTT_LONG,
    NETKVM_RTT_EDIT,
    NETKVM_RTT_LAST
};

#define NETKVM_RTT_UNKNOWN NETKVM_RTT_LAST

////////////////////////////////////////////////////////////////////////////////
// NOTE: Basic Parameter Info can be accessed via the public neTKVMRegParam API:
//  neTKVMRegParam::GetName()
//  neTKVMRegParam::GetDescription()
//  neTKVMRegParam::IsOptional()
//  neTKVMRegParam::GetType()
//  neTKVMRegParam::GetValue()
// Extended Parameter Info can be accessed via the neTKVMRegParam::FillExInfo API.
////////////////////////////////////////////////////////////////////////////////

enum neTKVMRegParamExInfoIDType
{
    NETKVM_RPIID_ENUM_VALUE,      // Always followed by NETKVM_RPIID_ENUM_VALUE_DESC
    NETKVM_RPIID_ENUM_VALUE_DESC, // Always follows the NETKVM_RPIID_ENUM_VALUE
    NETKVM_RPIID_NUM_MIN,
    NETKVM_RPIID_NUM_MAX,
    NETKVM_RPIID_NUM_STEP,
    NETKVM_RPIID_EDIT_TEXT_LIMIT,
    NETKVM_RPIID_EDIT_UPPER_CASE,
    NETKVM_RPIID_LAST
};

typedef pair <neTKVMRegParamExInfoIDType, tstring> neTKVMRegParamExInfo;

typedef list<neTKVMRegParamExInfo> neTKVMRegParamExInfoList;

class neTKVMRegParamBadNameException : public neTKVMException
{
public:
    neTKVMRegParamBadNameException() :
      neTKVMException(TEXT("Invalid Registry Parameter Name"))
    { }
};

class neTKVMRegParamBadTypeException : public neTKVMException
{
public:
    neTKVMRegParamBadTypeException() :
      neTKVMException(TEXT("Invalid Registry Parameter Type"))
    { }
};

class neTKVMRegParamBadRegistryException : public neTKVMException
{
public:
    neTKVMRegParamBadRegistryException() :
      neTKVMException(TEXT("Invalid Registry Parameter Data"))
    { }
};

class neTKVMRegParam
{
public:
    static neTKVMRegParamType GetType(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
    static neTKVMRegParam    *GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
    static neTKVMRegParam    *GetParam(neTKVMRegAccess &DevParamsRegKey, DWORD dwIndex);

    virtual ~neTKVMRegParam(void);

    const tstring &GetName(void) const { return m_Name; }
    const tstring &GetValue(void) const { return m_Value; }
    const tstring &GetDescription(void) const { return m_Description; }
    bool           IsOptional(void) const { return m_bOptional; }

    bool ValidateAndSetValue(LPCTSTR pszValue)
    {
        if (ValidateValue(pszValue))
        {
            SetValue(pszValue);
            return true;
        }

        return false;
    }

    bool Save(void);

    virtual void FillExInfo(neTKVMRegParamExInfoList &ExInfoList);

protected:
    neTKVMRegParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    void SetValue(LPCTSTR pszValue)
    {
        m_Value = pszValue;
    }

    void SetDescription(LPCTSTR pszDescription)
    {
        m_Description = pszDescription;
    }

    virtual neTKVMRegParamType GetType(void) const = 0;
    virtual bool            ValidateValue(LPCTSTR pszValue) = 0;
    virtual void            Load(void);

    tstring       m_Name;
    tstring       m_Description;
    tstring       m_Value;
    bool          m_bOptional;
    tstring       m_ParamRegSubKey;
    neTKVMRegAccess &m_DevParamsRegKey;
};

class neTKVMRegEnumParam : public neTKVMRegParam
{
    friend static neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual void FillExInfo(neTKVMRegParamExInfoList &ExInfoList);

protected:
    neTKVMRegEnumParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTKVMRegParamType GetType(void) const
    {
        return NETKVM_RTT_ENUM;
    }

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    neTKVMTStrList m_Values;
    neTKVMTStrList m_ValueDescs;
};

template <class INT_T>
class neTKVMRegNumParam : public neTKVMRegParam
{
    friend static neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual ~neTKVMRegNumParam();

    virtual void FillExInfo(neTKVMRegParamExInfoList &ExInfoList);

protected:
    neTKVMRegNumParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    INT_T m_nMin;
    INT_T m_nMax;
    INT_T m_nStep;
};

class neTKVMRegIntParam : public neTKVMRegNumParam<unsigned int>
{
    friend static neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
protected:
    neTKVMRegIntParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTKVMRegParamType GetType(void) const
    {
        return NETKVM_RTT_INT;
    }
};

class neTKVMRegLongParam : public neTKVMRegNumParam<unsigned long>
{
    friend static neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
protected:
    neTKVMRegLongParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTKVMRegParamType GetType(void) const
    {
        return NETKVM_RTT_LONG;
    }
};

class neTKVMRegEditParam : public neTKVMRegParam
{
    friend static neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);
public:
    virtual void FillExInfo(neTKVMRegParamExInfoList &ExInfoList);

protected:
    neTKVMRegEditParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName);

    virtual neTKVMRegParamType GetType(void) const
    {
        return NETKVM_RTT_EDIT;
    }

    virtual bool ValidateValue(LPCTSTR pszValue);
    virtual void Load(void);

    DWORD m_nLimitText;
    bool  m_bUpperCase;
};

