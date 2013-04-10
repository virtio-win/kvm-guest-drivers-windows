#include "StdAfx.h"
#include "RegParam.h"

#define REG_PATH_DELIMITER              TEXT("\\")

#define REG_DEV_PARAMS_KNAME            TEXT("Ndi\\Params")

#define REG_PARAM_DESC_VNAME            TEXT("ParamDesc")
#define REG_PARAM_DEFAULT_VNAME         TEXT("Default")
#define REG_PARAM_TYPE_VNAME            TEXT("Type")
#define REG_PARAM_OPT_VNAME             TEXT("Optional")

#define REG_PARAM_ENUM_VALUES_KNAME     TEXT("Enum")

#define REG_PARAM_NUM_TYPE_MIN_VNAME    TEXT("Min")
#define REG_PARAM_NUM_TYPE_MAX_VNAME    TEXT("Max")
#define REG_PARAM_NUM_TYPE_STEP_VNAME   TEXT("Step")

#define REG_PARAM_EDIT_TYPE_LIM_VNAME   TEXT("LimitText")
#define REG_PARAM_EDIT_TYPE_UCASE_VNAME TEXT("UpperCase")

#define REG_PARAM_INFO_DELIMETER        TEXT("-----------------------------------------------------")
#define REG_PARAM_INFO_IDENT            TEXT("  ")

static const LPCTSTR RegParamTypes[] =
{
    TEXT("enum"),
    TEXT("int"),
    TEXT("long"),
    TEXT("edit")
};

static BOOL  ReadStringDWord(neTKVMRegAccess &DevParamsRegKey,
                             LPCTSTR       lpzValueName,
                             LPDWORD       lpdwValue,
                             LPCTSTR       lpzSubKey)
{
    BOOL  bRes = FALSE;
    TCHAR tcaBuf[DEFAULT_REG_ENTRY_DATA_LEN];

    bRes = (DevParamsRegKey.ReadString(lpzValueName, tcaBuf, TBUF_SIZEOF(tcaBuf), lpzSubKey) != 0);
    if (bRes == TRUE)
    {
        LPTSTR lpPos = NULL;
        DWORD  dwVal = _tcstoul(tcaBuf, &lpPos, 0);

        if (*lpPos != 0)
        {
            throw neTKVMRegParamBadRegistryException();
        }

        *lpdwValue = dwVal;
    }

    return bRes;
}


static DWORD ReadStringDWord(neTKVMRegAccess &DevParamsRegKey,
                             LPCTSTR       lpzValueName,
                             DWORD         dwDefault,
                             LPCTSTR       lpzSubKey)
{
    DWORD dwRes = 0;
    return ReadStringDWord(DevParamsRegKey, lpzValueName, &dwRes, lpzSubKey)?dwRes:dwDefault;
}

neTKVMRegParamType neTKVMRegParam::GetType(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
{
    neTKVMRegParamType eRes = NETKVM_RTT_UNKNOWN;
    TCHAR           tcaBuf[DEFAULT_REG_ENTRY_DATA_LEN];
    tstring         ParamRegSubKey(REG_DEV_PARAMS_KNAME);

    if (pszName == NULL)
    {
        throw neTKVMRegParamBadNameException();
    }

    ParamRegSubKey += REG_PATH_DELIMITER;
    ParamRegSubKey += pszName;

    if (DevParamsRegKey.ReadString(REG_PARAM_TYPE_VNAME, tcaBuf, TBUF_SIZEOF(tcaBuf), ParamRegSubKey.c_str()) != 0)
    {
        for (int i = 0; i < ARRAY_SIZE(RegParamTypes); i++)
        {
            if (_tcsicmp(tcaBuf, RegParamTypes[i]) == 0)
            {
                eRes = (neTKVMRegParamType)i;
                break;
            }
        }
    }

    return eRes;
}

neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
{
    neTKVMRegParam    *Param = NULL;
    neTKVMRegParamType eParamType = neTKVMRegParam::GetType(DevParamsRegKey, pszName);
    switch (eParamType)
    {
    case NETKVM_RTT_ENUM:
        Param = new neTKVMRegEnumParam(DevParamsRegKey, pszName);
        break;
    case NETKVM_RTT_INT:
        Param = new neTKVMRegIntParam(DevParamsRegKey, pszName);
        break;
    case NETKVM_RTT_LONG:
        Param = new neTKVMRegLongParam(DevParamsRegKey, pszName);
        break;
    case NETKVM_RTT_EDIT:
        Param = new neTKVMRegEditParam(DevParamsRegKey, pszName);
        break;
    default:
        break;
    }

    if (Param)
    {
        Param->Load();

        if (Param->ValidateValue(Param->GetValue().c_str()) == false)
        {
            throw neTKVMRegParamBadRegistryException();
        }
    }

    return Param;
}

neTKVMRegParam *neTKVMRegParam::GetParam(neTKVMRegAccess &DevParamsRegKey, DWORD dwIndex)
{
    neTKVMRegParam *Param = NULL;
    TCHAR        tcaBuf[DEFAULT_REG_ENTRY_DATA_LEN];

    if (DevParamsRegKey.ReadKeyName(tcaBuf,
                                    TBUF_SIZEOF(tcaBuf),
                                    dwIndex,
                                    REG_DEV_PARAMS_KNAME) == TRUE)
    {
        Param = neTKVMRegParam::GetParam(DevParamsRegKey, tcaBuf);
    }

    return Param;
}

neTKVMRegParam::neTKVMRegParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    :   m_bOptional(false), m_DevParamsRegKey(DevParamsRegKey), m_ParamRegSubKey(REG_DEV_PARAMS_KNAME)
{
    if (pszName)
    {
        m_Name = pszName;
    }

    if (m_Name.empty())
    {
        throw neTKVMRegParamBadNameException();
    }

    m_ParamRegSubKey += REG_PATH_DELIMITER;
    m_ParamRegSubKey += pszName;
}


neTKVMRegParam::~neTKVMRegParam(void)
{

}

void neTKVMRegParam::Load(void)
{
    TCHAR tcaBuf[DEFAULT_REG_ENTRY_DATA_LEN];
    DWORD dwRes;

    if (GetType(m_DevParamsRegKey, m_Name.c_str()) != GetType())
    {
        throw neTKVMRegParamBadTypeException();
    }

    m_bOptional = ReadStringDWord(m_DevParamsRegKey, REG_PARAM_OPT_VNAME, (DWORD)false, m_ParamRegSubKey.c_str())?true:false;

    // Read Value
    dwRes = m_DevParamsRegKey.ReadString(m_Name.c_str(), tcaBuf, TBUF_SIZEOF(tcaBuf));
    if (dwRes == 0)
    { // There's no value => Read Default
        dwRes = m_DevParamsRegKey.ReadString(REG_PARAM_DEFAULT_VNAME, tcaBuf, TBUF_SIZEOF(tcaBuf), m_ParamRegSubKey.c_str());
    }

    if (dwRes != 0)
    {
        SetValue(tcaBuf);
    }
    else if (m_bOptional == false)
    {
        throw neTKVMRegParamBadRegistryException();
    }

    if (m_DevParamsRegKey.ReadString(REG_PARAM_DESC_VNAME, tcaBuf, TBUF_SIZEOF(tcaBuf), m_ParamRegSubKey.c_str()) != 0)
    {
        SetDescription(tcaBuf);
    }
}

bool neTKVMRegParam::Save(void)
{
    return (m_DevParamsRegKey.WriteString(m_Name.c_str(), m_Value.c_str()) == TRUE)?true:false;
}

void neTKVMRegParam::FillExInfo(neTKVMRegParamExInfoList &ExInfoList)
{
    ExInfoList.clear();
}

neTKVMRegEnumParam::neTKVMRegEnumParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    : neTKVMRegParam(DevParamsRegKey, pszName)
{

}

bool neTKVMRegEnumParam::ValidateValue(LPCTSTR pszValue)
{
    bool                  bRes = false;
    neTKVMTStrList::iterator i;

    for (i = m_Values.begin(); bRes == false && i != m_Values.end(); i++)
    {
        bRes = _tcsicmp((*i).c_str(), pszValue) == 0;
    }

    return bRes;
}

void neTKVMRegEnumParam::Load(void)
{
    DWORD   dwI = 0;
    TCHAR   tcaBuf[DEFAULT_REG_ENTRY_DATA_LEN];
    tstring EnumValuesSubKey = m_ParamRegSubKey + REG_PATH_DELIMITER + REG_PARAM_ENUM_VALUES_KNAME;

    neTKVMRegParam::Load();

    while (m_DevParamsRegKey.ReadValueName(tcaBuf, TBUF_SIZEOF(tcaBuf), dwI++, EnumValuesSubKey.c_str()))
    {
        tstring EnumVName(tcaBuf);
        m_Values.push_back(EnumVName);
        if (m_DevParamsRegKey.ReadString(EnumVName.c_str(), tcaBuf, TBUF_SIZEOF(tcaBuf), EnumValuesSubKey.c_str()) == 0)
        {
            throw neTKVMRegParamBadRegistryException();
        }

        m_ValueDescs.push_back(tcaBuf);
    }
}
void neTKVMRegEnumParam::FillExInfo(neTKVMRegParamExInfoList &ExInfoList)
{
    neTKVMTStrList::iterator i;
    neTKVMTStrList::iterator j;

    neTKVMRegParam::FillExInfo(ExInfoList);

    for (i = m_Values.begin(), j = m_ValueDescs.begin();
         i!= m_Values.end() && j != m_ValueDescs.end();
         i++, j++)
    {
        ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_ENUM_VALUE, *i));
        ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_ENUM_VALUE_DESC, *j));
    }
}

template <class INT_T>
neTKVMRegNumParam<INT_T>::neTKVMRegNumParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    : m_nMin(0), m_nMax((INT_T)-1), m_nStep(0), neTKVMRegParam(DevParamsRegKey, pszName)
{

}

template <class INT_T>
neTKVMRegNumParam<INT_T>::~neTKVMRegNumParam()
{

}

template <class INT_T>
bool neTKVMRegNumParam<INT_T>::ValidateValue(LPCTSTR pszValue)
{
    bool bRes = false;

    if (pszValue)
    {
        LPTSTR psTmp  = NULL;
        INT_T  nValue = _tcstol(pszValue, &psTmp, 0);

        if (*psTmp == 0 && nValue >= m_nMin && nValue <= m_nMax)
        {
            bRes = (((nValue - m_nMin) % m_nStep) == 0);
        }
    }

    return bRes;
}

template <class INT_T>
void neTKVMRegNumParam<INT_T>::Load(void)
{
    neTKVMRegParam::Load();

    m_nMin = (INT_T)ReadStringDWord(m_DevParamsRegKey, REG_PARAM_NUM_TYPE_MIN_VNAME, (DWORD)0, m_ParamRegSubKey.c_str());
    m_nMax = (INT_T)ReadStringDWord(m_DevParamsRegKey, REG_PARAM_NUM_TYPE_MAX_VNAME, (DWORD)-1, m_ParamRegSubKey.c_str());
    m_nStep = (INT_T)ReadStringDWord(m_DevParamsRegKey, REG_PARAM_NUM_TYPE_STEP_VNAME, (DWORD)1, m_ParamRegSubKey.c_str());
}

template <class INT_T>
void neTKVMRegNumParam<INT_T>::FillExInfo(neTKVMRegParamExInfoList &ExInfoList)
{
    tstringstream tss;

    neTKVMRegParam::FillExInfo(ExInfoList);

    tss << m_nMin;
    ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_NUM_MIN, tss.str()));

    tss.str(tstring());
    tss << m_nMax;
    ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_NUM_MAX, tss.str()));

    tss.str(tstring());
    tss << m_nStep;
    ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_NUM_STEP, tss.str()));
}

neTKVMRegIntParam::neTKVMRegIntParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    : neTKVMRegNumParam<unsigned int>(DevParamsRegKey, pszName)
{

}

neTKVMRegLongParam::neTKVMRegLongParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    : neTKVMRegNumParam<unsigned long>(DevParamsRegKey, pszName)
{

}

neTKVMRegEditParam::neTKVMRegEditParam(neTKVMRegAccess &DevParamsRegKey, LPCTSTR pszName)
    : m_nLimitText(0), m_bUpperCase(false), neTKVMRegParam(DevParamsRegKey, pszName)
{

}

bool neTKVMRegEditParam::ValidateValue(LPCTSTR pszValue)
{
    bool bRes = false;

    if (pszValue == NULL)
    {
        goto End;
    }

    if (m_nLimitText && _tcslen(pszValue) > m_nLimitText)
    {
        goto End;
    }

    if (m_bUpperCase)
    {
        while (*pszValue)
        {
            if (!_istupper(*pszValue))
            {
                goto End;
            }
            pszValue++;
        }
    }

    bRes = true;

End:
    return bRes;
}

void neTKVMRegEditParam::Load(void)
{
    neTKVMRegParam::Load();

    m_nLimitText = ReadStringDWord(m_DevParamsRegKey, REG_PARAM_EDIT_TYPE_LIM_VNAME, m_nLimitText, m_ParamRegSubKey.c_str());
    m_bUpperCase = ReadStringDWord(m_DevParamsRegKey, REG_PARAM_EDIT_TYPE_UCASE_VNAME, (DWORD)false, m_ParamRegSubKey.c_str())?true:false;
}

void neTKVMRegEditParam::FillExInfo(neTKVMRegParamExInfoList &ExInfoList)
{
    tstringstream tss;

    neTKVMRegParam::FillExInfo(ExInfoList);

    tss << m_bUpperCase;
    ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_EDIT_UPPER_CASE, tss.str()));

    if (m_nLimitText)
    {
        tss.str(tstring());
        tss << m_nLimitText;
        ExInfoList.push_back(neTKVMRegParamExInfo(NETKVM_RPIID_EDIT_TEXT_LIMIT, tss.str()));
    }
}


