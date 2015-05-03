#include "stdafx.h"
#include "NetKVMnetsh.h"
#include "RegAccess.h"
#include "NetKVMAux.h"
#include "RegParam.h"

//This is NetSH Helper GUID {D9C599C4-8DCF-4a6a-93AA-A16FE6D5125C}
static const GUID NETKVM_HELPER_GUID =
    { 0xd9c599c4, 0x8dcf, 0x4a6a, { 0x93, 0xaa, 0xa1, 0x6f, 0xe6, 0xd5, 0x12, 0x5c } };
static const DWORD NETKVM_HELPER_VERSION = 1;

static const LPCTSTR NETKVM_HELPER_NAME = TEXT("NetKVM");
static const LPWSTR NETKVM_HELPER_NAME_W = L"NetKVM";

static const LPCTSTR NETKVM_DEVICES_CLASS = TEXT("net");

//TODO: put real device numbers here
static const LPCTSTR NETKVM_DEVICE_VENDOR_ID = TEXT("");
static const LPCTSTR NETKVM_DEVICE_HARDWARE_ID = TEXT("");

static const LPCWSTR NETKVM_IDX_PARAM_NAME   = L"idx";
static const LPCTSTR NETKVM_IDX_PARAM_NAME_T   = TEXT("idx");
static const LPCWSTR NETKVM_PARAM_PARAM_NAME = L"param";
static const LPCTSTR NETKVM_PARAM_PARAM_NAME_T = TEXT("param");
static const LPCWSTR NETKVM_VALUE_PARAM_NAME = L"value";
static const LPCTSTR NETKVM_VALUE_PARAM_NAME_T = TEXT("value");

static HINSTANCE g_hinstThisDLL = NULL;

static bool _NetKVMGetDeviceClassGuids(vector<GUID>& GUIDs)
{
    GUID *pguidDevClassPtr = NULL;
    DWORD dwNumGuids;

    if(!SetupDiClassGuidsFromNameEx(NETKVM_DEVICES_CLASS, NULL, 0,
                                    &dwNumGuids, NULL, NULL))
    {
        DWORD dwErr = GetLastError();

        if(ERROR_INSUFFICIENT_BUFFER == dwErr)
        {
            /* On first attemp we pass NULL and 0 as GUID array and GUID array size respectively;
            according to SetupDiClassGuidsFromNameEx, the function sets RequiredSize output formal parameter (&dwNumGuids)
            to the desired GUID's array size. The static analyzer indicates an error when output parameter from
            failed funciton is used, so the warning is suppressed */
#pragma warning(suppress: 6102)
            pguidDevClassPtr = new GUID[dwNumGuids];
        }
        else
        {
            NETCO_DEBUG_PRINT(TEXT("SetupDiClassGuidsFromNameEx failed with code ") << dwErr);
            return false;
        }

        if(!SetupDiClassGuidsFromNameEx(NETKVM_DEVICES_CLASS, pguidDevClassPtr, dwNumGuids,
                                        &dwNumGuids, NULL, NULL))
        {
            delete [] pguidDevClassPtr;
            NETCO_DEBUG_PRINT(TEXT("SetupDiClassGuidsFromNameEx failed with code ") << dwErr);
            return false;
        }
    }

    GUIDs.insert(GUIDs.end(), &pguidDevClassPtr[0], &pguidDevClassPtr[dwNumGuids]);
    delete [] pguidDevClassPtr;
    return true;
}

tstring _NetKVMQueryDeviceString(HDEVINFO hDeviceSet, PSP_DEVINFO_DATA DeviceInfoData, DWORD dwPropertyID)
{
    LPTSTR szDeviceString = NULL;
    DWORD dwSize = 0;
    DWORD dwDataType;

    while(!SetupDiGetDeviceRegistryProperty(hDeviceSet, DeviceInfoData, dwPropertyID,
        &dwDataType, (PBYTE)szDeviceString, dwSize, &dwSize))
    {
        DWORD dwErr = GetLastError();

        if(ERROR_INSUFFICIENT_BUFFER != dwErr)
        {
            delete [] szDeviceString;
            NETCO_DEBUG_PRINT(TEXT("SetupDiGetDeviceRegistryProperty failed with code ") << dwErr);
            return tstring();
        }
        /* According to SetupDiGetDeviceRegistryProperty, the RequiredSize output parameter (&dwSize) is set
          to the required size of PropertyBuffer (szDeviceString) parameter. The static analyzer indicates
          error when the output paramter from failed function, so the warnings is suppressed */
#pragma warning(suppress: 6102)
        szDeviceString = new TCHAR[(dwSize/sizeof(TCHAR))+1];
    }
    if (REG_SZ != dwDataType)
    {
#pragma warning(suppress: 6102)
        delete[] szDeviceString;
        NETCO_DEBUG_PRINT(TEXT("SetupDiGetDeviceRegistryProperty(string) returned incorrect data type ") << dwDataType);
        return tstring();
    }

#pragma warning(suppress: 6102)
#pragma warning(suppress: 6011)
    szDeviceString[dwSize/sizeof(TCHAR)] = TEXT('\0');
    return tstring(szDeviceString);
}

DWORD _NetKVMQueryDeviceDWORD(HDEVINFO hDeviceSet, PSP_DEVINFO_DATA DeviceInfoData, DWORD dwPropertyID)
{
    DWORD dwDeviceDword;
    DWORD dwDataType;

    if(!SetupDiGetDeviceRegistryProperty(hDeviceSet, DeviceInfoData, dwPropertyID,
        &dwDataType, (PBYTE)&dwDeviceDword, sizeof(dwDeviceDword), NULL))
    {
        return 0;
    }
    if (REG_DWORD != dwDataType)
    {
        NETCO_DEBUG_PRINT(TEXT("SetupDiGetDeviceRegistryProperty(DWORD) returned incorrect data type ") << REG_DWORD);
        return 0;
    }
#pragma warning(suppress: 6102)
    return dwDeviceDword;
}

static HKEY _NetKVMOpenDeviceSwRegKey(HDEVINFO hDeviceSet, PSP_DEVINFO_DATA DeviceInfoData)
{
    HKEY hRes = SetupDiOpenDevRegKey(hDeviceSet, DeviceInfoData, DICS_FLAG_GLOBAL,
                                     0, DIREG_DRV, KEY_READ);

    if(INVALID_HANDLE_VALUE == hRes)
    {
        PrintError(g_hinstThisDLL, IDS_CANNOTOPENKEY);
        tcout << TEXT("Error: ") << GetLastError() << endl;
    }

    return hRes;
}

typedef struct tag_NetKVMDeviceInfo
{
    tstring strDeviceID;
    tstring strDeviceDescription;
    tstring strDeviceFriendlyName;
    tstring strLocationInfo;
    tstring strRegPathName;
    DWORD   dwDeviceNumber;
    SP_DEVINFO_DATA DevInfoData;
} _NetKVMDeviceInfo, *_PNetKVMDeviceInfo;

bool _NetKVMIsKnownDevice(const tstring &strDeviceID)
{
    return (tstring::npos != strDeviceID.find(tstring(TEXT("VEN_")) + NETKVM_DEVICE_VENDOR_ID)) &&
           (tstring::npos != strDeviceID.find(tstring(TEXT("DEV_")) + NETKVM_DEVICE_HARDWARE_ID));
}

tstring _NetKVMKeyPathFromDeviceSwHKEY(HKEY hKey)
{
    tstring strNativePath = wstring2tstring(NetKVMGetKeyPathFromKKEY(hKey));
    tstring strNativePrefix = TEXT("\\REGISTRY\\MACHINE\\");
    if(strNativePath.substr(0, strNativePrefix.length()) == strNativePrefix)
    {
        return strNativePath.substr(strNativePrefix.length());
    }
    else return tstring();
}


static bool _NetKVMGetKnownDevices(GUID *pguidDevClassPtr, vector<_NetKVMDeviceInfo>& Devices, HDEVINFO *phDeviceInfo)
{
    *phDeviceInfo = SetupDiGetClassDevsEx(pguidDevClassPtr, NULL, NULL, DIGCF_PRESENT, NULL, NULL, NULL);
    if(INVALID_HANDLE_VALUE == *phDeviceInfo)
    {
        DWORD dwErr = GetLastError();
        UNREFERENCED_PARAMETER(dwErr);
        NETCO_DEBUG_PRINT(TEXT("SetupDiGetClassDevsEx failed with code ") << dwErr);
        return false;
    }

    SP_DEVINFO_LIST_DETAIL_DATA DevListInfo;
    DevListInfo.cbSize = sizeof(DevListInfo);
    if(!SetupDiGetDeviceInfoListDetail(*phDeviceInfo, &DevListInfo))
    {
        DWORD dwErr = GetLastError();
        UNREFERENCED_PARAMETER(dwErr);
        NETCO_DEBUG_PRINT(TEXT("SetupDiGetDeviceInfoListDetail failed with code ") << dwErr);
        SetupDiDestroyDeviceInfoList(*phDeviceInfo);
        return false;
    }

    SP_DEVINFO_DATA CurrDeviceInfo;
    CurrDeviceInfo.cbSize = sizeof(CurrDeviceInfo);
    for(DWORD dwDevIndex = 0; SetupDiEnumDeviceInfo(*phDeviceInfo, dwDevIndex, &CurrDeviceInfo); dwDevIndex++)
    {
        TCHAR szDeviceId[MAX_DEVICE_ID_LEN];
        CONFIGRET err = CM_Get_Device_ID_Ex(CurrDeviceInfo.DevInst,
                                            szDeviceId, MAX_DEVICE_ID_LEN,
                                            0, DevListInfo.RemoteMachineHandle);
        if(CR_SUCCESS == err)
        {
            if(_NetKVMIsKnownDevice(szDeviceId))
            {
                _NetKVMDeviceInfo ResDevInfo;
                ResDevInfo.strDeviceID           = szDeviceId;
                ResDevInfo.strDeviceDescription  = _NetKVMQueryDeviceString(*phDeviceInfo, &CurrDeviceInfo, SPDRP_DEVICEDESC);
                ResDevInfo.strDeviceFriendlyName = _NetKVMQueryDeviceString(*phDeviceInfo, &CurrDeviceInfo, SPDRP_FRIENDLYNAME);
                if (ResDevInfo.strDeviceFriendlyName.length() == 0)
                {
                    ResDevInfo.strDeviceFriendlyName = ResDevInfo.strDeviceDescription;
                }
                ResDevInfo.strLocationInfo = _NetKVMQueryDeviceString(*phDeviceInfo, &CurrDeviceInfo, SPDRP_LOCATION_INFORMATION);
                ResDevInfo.dwDeviceNumber = _NetKVMQueryDeviceDWORD(*phDeviceInfo, &CurrDeviceInfo, SPDRP_UI_NUMBER);
                ResDevInfo.DevInfoData = CurrDeviceInfo;
                HKEY hSoftwareRegKey = _NetKVMOpenDeviceSwRegKey(*phDeviceInfo, &CurrDeviceInfo);
                if(INVALID_HANDLE_VALUE != hSoftwareRegKey)
                {
                    ResDevInfo.strRegPathName = _NetKVMKeyPathFromDeviceSwHKEY(hSoftwareRegKey);
                    CloseHandle(hSoftwareRegKey);
                    if(!ResDevInfo.strRegPathName.empty())
                        Devices.push_back(ResDevInfo);
                }
            }
        }
        else
        {
            NETCO_DEBUG_PRINT(TEXT("CM_Get_Device_ID_Ex failed with code ") << err);
            return false;
        }
    }

    DWORD dwErr = GetLastError();
    if(ERROR_NO_MORE_ITEMS != dwErr)
    {
        NETCO_DEBUG_PRINT(TEXT("SetupDiGetDeviceInfoListDetail failed with code ") << dwErr);
        SetupDiDestroyDeviceInfoList(*phDeviceInfo);
        return false;
    }

    return true;
}

static pair< HDEVINFO, vector<_NetKVMDeviceInfo> > _NetKVMGetDevicesOfInterest()
{
    vector<GUID> GUIDs;
    pair< HDEVINFO, vector<_NetKVMDeviceInfo> > Devices;

    _NetKVMGetDeviceClassGuids(GUIDs);
    if(GUIDs.empty())
        return Devices;

    _NetKVMGetKnownDevices(&GUIDs[0], Devices.second, &Devices.first);
    return Devices;
}

void _NetKVMDumpDeviceInfo(DWORD dwIndex, const _NetKVMDeviceInfo &DeviceInfo)
{
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICEID);
    tcout << TEXT(": ") << DeviceInfo.strDeviceID << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICEIDX);
    tcout << TEXT(": ") << dwIndex << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICEDESC);
    tcout << TEXT(": ") << DeviceInfo.strDeviceDescription << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICEFRNAME);
    tcout << TEXT(": ") << DeviceInfo.strDeviceFriendlyName << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICELOCINFO);
    tcout << TEXT(": ") << DeviceInfo.strLocationInfo << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICENUM);
    tcout << TEXT(": ") << DeviceInfo.dwDeviceNumber << endl;

    tcout << TEXT("\t");
    PrintMessageFromModule(g_hinstThisDLL, IDS_DEVICEREGKEY);
    tcout << TEXT(": ") << DeviceInfo.strRegPathName << endl << endl;
}

HDEVINFO g_hDeviceInfoList = INVALID_HANDLE_VALUE;
static vector<_NetKVMDeviceInfo> g_DevicesOfInterest;

static DWORD _NetKVMRestartDevice(DWORD dwIndex)
{
    SP_PROPCHANGE_PARAMS Params;
    Params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    Params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    Params.StateChange = DICS_PROPCHANGE;
    Params.Scope = DICS_FLAG_CONFIGSPECIFIC;
    Params.HwProfile = 0;

    if(!SetupDiSetClassInstallParams(g_hDeviceInfoList,
                                     &g_DevicesOfInterest[dwIndex].DevInfoData,
                                     &Params.ClassInstallHeader,
                                     sizeof(Params)))
    {
        return GetLastError();
    }

    if(!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,
                                  g_hDeviceInfoList,
                                  &g_DevicesOfInterest[dwIndex].DevInfoData))
    {
        return GetLastError();
    }

    SP_DEVINSTALL_PARAMS DeviceInstallParams;
    DeviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);

    if(!SetupDiGetDeviceInstallParams(g_hDeviceInfoList,
                                      &g_DevicesOfInterest[dwIndex].DevInfoData,
                                      &DeviceInstallParams))
    {
        return ERROR_SUCCESS_REBOOT_REQUIRED;
    }

    if(DeviceInstallParams.Flags & (DI_NEEDRESTART|DI_NEEDREBOOT))
    {
        return ERROR_SUCCESS_REBOOT_REQUIRED;
    }

    return NO_ERROR;
}

static DWORD __NetKVMwsz2DWORD(PWSTR wszInput)
{
    wstringstream wstrmConverter;
    wstrmConverter << wszInput;
    DWORD dwIndex;
    wstrmConverter >> dwIndex;
    return dwIndex;
}

static BOOL __NetKVMConvertDeviceIndex(PWSTR wszIndex, PDWORD pdwIndex)
{
    *pdwIndex = __NetKVMwsz2DWORD(wszIndex);
    if(*pdwIndex >= g_DevicesOfInterest.size())
    {
        PrintMessageFromModule(g_hinstThisDLL, IDS_INDEXOUTOFRANGE);
        tcout << endl;
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

typedef pair<tstring, tstring> ParamDescrT;
typedef list< ParamDescrT > ParamDescrListT;

static void _NetKVMFillDeviceParamsList(DWORD dwDeviceIndex, ParamDescrListT& List)
{
    auto_ptr<neTKVMRegParam> pParam;
    DWORD dwParamIndex = 0;
    neTKVMRegAccess DeviceRegKey(HKEY_LOCAL_MACHINE, g_DevicesOfInterest[dwDeviceIndex].strRegPathName.c_str());

    while(NULL != (pParam = auto_ptr<neTKVMRegParam>(neTKVMRegParam::GetParam(DeviceRegKey, dwParamIndex++))).get())
    {
        ParamDescrT ParamDescr;
        ParamDescr.first = pParam->GetName();
        ParamDescr.second = pParam->GetDescription();
        List.push_back(ParamDescr);
    }
}

typedef pair<tstring, tstring> ParamValueT;
typedef list< ParamValueT > ParamValueListT;

static void _NetKVMFillDeviceValuesList(DWORD dwDeviceIndex, ParamValueListT& List)
{
    auto_ptr<neTKVMRegParam> pParam;
    DWORD dwParamIndex = 0;
    neTKVMRegAccess DeviceRegKey(HKEY_LOCAL_MACHINE, g_DevicesOfInterest[dwDeviceIndex].strRegPathName.c_str());

    while(NULL != (pParam = auto_ptr<neTKVMRegParam>(neTKVMRegParam::GetParam(DeviceRegKey, dwParamIndex++))).get())
    {
        ParamValueT ParamValue;
        ParamValue.first = pParam->GetName();
        ParamValue.second = pParam->GetValue();
        List.push_back(ParamValue);
    }
}

static bool _NetKVMQueryDetailedParamInfo(DWORD dwDeviceIndex,
                                          const tstring &strParamName,
                                          neTKVMRegParamType &ParamType,
                                          neTKVMRegParamExInfoList &ParamInfo)
{
    neTKVMRegAccess DeviceRegKey(HKEY_LOCAL_MACHINE, g_DevicesOfInterest[dwDeviceIndex].strRegPathName.c_str());

    auto_ptr<neTKVMRegParam> pParam(neTKVMRegParam::GetParam(DeviceRegKey, strParamName.c_str()));

    ParamType = neTKVMRegParam::GetType(DeviceRegKey, strParamName.c_str());
    if(NETKVM_RTT_UNKNOWN == ParamType)
    {
        return false;
    }

    if(NULL == pParam.get())
    {
        return false;
    }

    pParam->FillExInfo(ParamInfo);
    return true;
}

static bool _NetKVMQueryParamValue(DWORD dwDeviceIndex,
                                   const tstring &strParamName,
                                   tstring &strParamValue)
{
    neTKVMRegAccess DeviceRegKey(HKEY_LOCAL_MACHINE, g_DevicesOfInterest[dwDeviceIndex].strRegPathName.c_str());

    auto_ptr<neTKVMRegParam> pParam(neTKVMRegParam::GetParam(DeviceRegKey, strParamName.c_str()));
    if(NULL == pParam.get())
    {
        return false;
    }

    strParamValue = pParam->GetValue();
    return true;
}

static bool _NetKVMSetParamValue(DWORD dwDeviceIndex,
                                 const tstring &strParamName,
                                 const tstring &strParamValue)
{
    neTKVMRegAccess DeviceRegKey(HKEY_LOCAL_MACHINE, g_DevicesOfInterest[dwDeviceIndex].strRegPathName.c_str());

    auto_ptr<neTKVMRegParam> pParam(neTKVMRegParam::GetParam(DeviceRegKey, strParamName.c_str()));
    if(NULL == pParam.get())
    {
        return false;
    }

    if(!pParam->ValidateAndSetValue(strParamValue.c_str()))
    {
        return false;
    }

    return pParam->Save();
}

//
// Usage: show devices
//
// Remarks:
//
//      Lists all NetKVM devices currently present in the system.
//      Each device is assigned unique index.
//      The index is used to identify the device for all other NetKVM commands.
//
DWORD WINAPI _NetKVMShowDevicesCmdHandler(__in   PWCHAR  /*pwszMachine*/,
                                          __in   PWCHAR* /*ppwcArguments*/,
                                          __in   DWORD   /*dwCurrentIndex*/,
                                          __in   DWORD   /*dwArgCount*/,
                                          __in   DWORD   /*dwFlags*/,
                                          __in   PVOID   /*pvData*/,
                                          __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMShowDevicesCmdHandler called"));
        int i = 0;
        for(vector<_NetKVMDeviceInfo>::const_iterator it = g_DevicesOfInterest.begin();
            it != g_DevicesOfInterest.end();
            ++it, ++i)
        {
            _NetKVMDumpDeviceInfo(i, *it);
        }
        return NO_ERROR;
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

//
// Usage: show paraminfo [idx=]0-N [param=]name
//
// Parameters:
//
//      IDX - Specifies the device index as it is shown in "show devices" output.
//      PARAM - Specifies name of the parameter.
//
// Remarks:
//
//      Shows detailed information about specified parameter of specified device.
//
// Examples:
//
//      show paraminfo idx=0 param=window
//      show paraminfo 2 rx_buffers
//
DWORD WINAPI _NetKVMShowParamInfoCmdHandler (__in   PWCHAR  /*pwszMachine*/,
                                             __in   PWCHAR* ppwcArguments,
                                             __in   DWORD   dwCurrentIndex,
                                             __in   DWORD   dwArgCount,
                                             __in   DWORD   /*dwFlags*/,
                                             __in   PVOID   /*pvData*/,
                                             __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMShowParamInfoCmdHandler called"));
        TAG_TYPE TagsList[] =
            { {NETKVM_IDX_PARAM_NAME,   NS_REQ_PRESENT},
              {NETKVM_PARAM_PARAM_NAME, NS_REQ_PRESENT} };

        auto_ptr<DWORD> pdwTagMatchResults(new DWORD[dwArgCount - dwCurrentIndex]);
        DWORD dwPreprocessResult = PreprocessCommand(NULL, ppwcArguments,
                                                     dwCurrentIndex, dwArgCount,
                                                     TagsList, ARRAY_SIZE(TagsList),
                                                     ARRAY_SIZE(TagsList), ARRAY_SIZE(TagsList),
                                                     pdwTagMatchResults.get());

        switch (dwPreprocessResult)
        {
        case NO_ERROR:
            {
                DWORD dwIndex;
                if(__NetKVMConvertDeviceIndex(ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[0]], &dwIndex))
                {
                    wstring wstrParamName = ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[1]];
                    neTKVMRegParamExInfoList ParamInfo;
                    neTKVMRegParamType ParamType;

                    if(!_NetKVMQueryDetailedParamInfo(dwIndex, wstring2tstring(wstrParamName),ParamType, ParamInfo))
                    {
                        return ERROR_INVALID_PARAMETER;
                    }

                    PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMTYPE);
                    tcout << TEXT(": ");
                    switch(ParamType)
                    {
                    case NETKVM_RTT_ENUM:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMENUM);
                        break;
                    case NETKVM_RTT_INT:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMINT);
                        break;
                    case NETKVM_RTT_LONG:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMLONG);
                        break;
                    case NETKVM_RTT_EDIT:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMTEXT);
                        break;
                    default:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMUNKNOWN);
                        break;
                    }
                    tcout << endl;

                    for(neTKVMRegParamExInfoList::const_iterator it = ParamInfo.begin();
                        it != ParamInfo.end(); ++it)
                    {
                        switch(it->first)
                        {
                        case NETKVM_RPIID_ENUM_VALUE:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMENUMVALUE);
                            tcout << TEXT(" \"") << it->second << TEXT("\" - ");
                            break;
                        case NETKVM_RPIID_ENUM_VALUE_DESC:
                            tcout << it->second << endl;
                            break;
                        case NETKVM_RPIID_NUM_MIN:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMNUMMIN);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        case NETKVM_RPIID_NUM_MAX:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMNUMMAX);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        case NETKVM_RPIID_NUM_STEP:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMNUMSTEP);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        case NETKVM_RPIID_EDIT_TEXT_LIMIT:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMTEXTLIMIT);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        case NETKVM_RPIID_EDIT_UPPER_CASE:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMUPPERCASE);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        default:
                            tcout << TEXT("\t");
                            PrintMessageFromModule(g_hinstThisDLL, IDS_PARAMUNKNOWNPROP);
                            tcout << TEXT(": ") << it->second << endl;
                            break;
                        }
                    }
                    return NO_ERROR;
                }
                else
                {
                    return ERROR_INVALID_PARAMETER;
                }
            }
            __fallthrough;
        default:
            NETCO_DEBUG_PRINT(TEXT("PreprocessCommand returned: ") << dwPreprocessResult);
            return dwPreprocessResult;
        }
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

//
// Usage: getparam [idx=]0-N [param=]name
//
// Parameters:
//
//      IDX - Specifies the device index as it is shown in "show devices" output.
//      PARAM - Specifies name of the parameter.
//
// Remarks:
//
//      Retrieves given parameter value.
//
// Examples:
//
//      getparam idx=0 param=window
//      getparam 2 rx_buffers
//
DWORD WINAPI _NetKVMGetParamCmdHandler (__in   PWCHAR  /*pwszMachine*/,
                                        __in   PWCHAR* ppwcArguments,
                                        __in   DWORD   dwCurrentIndex,
                                        __in   DWORD   dwArgCount,
                                        __in   DWORD   /*dwFlags*/,
                                        __in   PVOID   /*pvData*/,
                                        __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMGetParamCmdHandler called"));
        TAG_TYPE TagsList[] =
            { {NETKVM_IDX_PARAM_NAME,   NS_REQ_PRESENT},
              {NETKVM_PARAM_PARAM_NAME, NS_REQ_PRESENT} };

        auto_ptr<DWORD> pdwTagMatchResults(new DWORD[dwArgCount - dwCurrentIndex]);
        DWORD dwPreprocessResult = PreprocessCommand(NULL, ppwcArguments,
                                                     dwCurrentIndex, dwArgCount,
                                                     TagsList, ARRAY_SIZE(TagsList),
                                                     ARRAY_SIZE(TagsList), ARRAY_SIZE(TagsList),
                                                     pdwTagMatchResults.get());

        switch (dwPreprocessResult)
        {
        case NO_ERROR:
            {
                DWORD dwIndex;
                if(__NetKVMConvertDeviceIndex(ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[0]], &dwIndex))
                {
                    wstring wstrParamName = ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[1]];
                    tstring strParamName = wstring2tstring(wstrParamName);
                    tstring strParamValue;

                    if(!_NetKVMQueryParamValue(dwIndex, strParamName, strParamValue))
                    {
                        return ERROR_INVALID_PARAMETER;
                    }
                    tcout << strParamName << TEXT(" = ") << strParamValue << endl;
                    return NO_ERROR;
                }
                else
                {
                    return ERROR_INVALID_PARAMETER;
                }
            }
            __fallthrough;
        default:
            NETCO_DEBUG_PRINT(TEXT("PreprocessCommand returned: ") << dwPreprocessResult);
            return dwPreprocessResult;
        }
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

//
// Usage: setparam [idx=]0-N [param=]name [value=]value
//
// Parameters:
//
//      IDX - Specifies the device index as it is shown in "show devices" output.
//      PARAM - Specifies name of the parameter.
//      VALUE - Specifies the value of the parameter.
//
// Remarks:
//
//      Set given parameter value.
//
// Examples:
//
//      setparam idx=0 param=window value=10
//      setparam 2 rx_buffers 45
//
DWORD WINAPI _NetKVMSetParamCmdHandler (__in   PWCHAR  /*pwszMachine*/,
                                        __in   PWCHAR* ppwcArguments,
                                        __in   DWORD   dwCurrentIndex,
                                        __in   DWORD   dwArgCount,
                                        __in   DWORD   /*dwFlags*/,
                                        __in   PVOID   /*pvData*/,
                                        __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMSetParamCmdHandler called"));
        TAG_TYPE TagsList[] =
            { {NETKVM_IDX_PARAM_NAME,   NS_REQ_PRESENT},
              {NETKVM_PARAM_PARAM_NAME, NS_REQ_PRESENT},
              {NETKVM_VALUE_PARAM_NAME, NS_REQ_PRESENT} };

        auto_ptr<DWORD> pdwTagMatchResults(new DWORD[dwArgCount - dwCurrentIndex]);
        DWORD dwPreprocessResult = PreprocessCommand(NULL, ppwcArguments,
                                                     dwCurrentIndex, dwArgCount,
                                                     TagsList, ARRAY_SIZE(TagsList),
                                                     ARRAY_SIZE(TagsList), ARRAY_SIZE(TagsList),
                                                     pdwTagMatchResults.get());

        switch (dwPreprocessResult)
        {
        case NO_ERROR:
            {
                DWORD dwIndex;
                if(__NetKVMConvertDeviceIndex(ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[0]], &dwIndex))
                {
                    wstring wstrParamName = ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[1]];
                    wstring wstrParamValue = ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[2]];

                    if(!_NetKVMSetParamValue(dwIndex, wstring2tstring(wstrParamName), wstring2tstring(wstrParamValue)))
                    {
                        return ERROR_INVALID_PARAMETER;
                    }
                    return NO_ERROR;
                }
                else
                {
                    return ERROR_INVALID_PARAMETER;
                }
            }
            __fallthrough;
        default:
            NETCO_DEBUG_PRINT(TEXT("PreprocessCommand returned: ") << dwPreprocessResult);
            return dwPreprocessResult;
        }
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}
//
// Usage: show parameters [idx=]0-N
//
// Parameters:
//
//      IDX - Specifies the device index as it is shown in "show devices" output.
//
// Remarks:
//
//      Shows parameters of device specified by index.
//
// Examples:
//
//      show parameters idx=0
//      show parameters 2
//
DWORD WINAPI _NetKVMShowParamsCmdHandler(__in   PWCHAR  /*pwszMachine*/,
                                         __in   PWCHAR* ppwcArguments,
                                         __in   DWORD   dwCurrentIndex,
                                         __in   DWORD   dwArgCount,
                                         __in   DWORD   /*dwFlags*/,
                                         __in   PVOID   /*pvData*/,
                                         __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMShowParamsCmdHandler called"));
        TAG_TYPE TagsList[] =
            { {NETKVM_IDX_PARAM_NAME,   NS_REQ_PRESENT} };

        auto_ptr<DWORD> pdwTagMatchResults(new DWORD[dwArgCount - dwCurrentIndex]);
        DWORD dwPreprocessResult = PreprocessCommand(NULL, ppwcArguments,
                                                     dwCurrentIndex, dwArgCount,
                                                     TagsList, ARRAY_SIZE(TagsList),
                                                     ARRAY_SIZE(TagsList), ARRAY_SIZE(TagsList),
                                                     pdwTagMatchResults.get());

        switch (dwPreprocessResult)
        {
        case NO_ERROR:
            {
                DWORD dwIndex;
                if(__NetKVMConvertDeviceIndex(ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[0]], &dwIndex))
                {
                    ParamDescrListT Params;

                    _NetKVMFillDeviceParamsList(dwIndex, Params);
                    for(ParamDescrListT::const_iterator it = Params.begin();
                        it != Params.end(); ++it)
                    {
                        tcout << it->first << endl<< TEXT("\t") << it->second << endl;
                    }
                    return NO_ERROR;
                }
                else
                {
                    return ERROR_INVALID_PARAMETER;
                }
            }
            __fallthrough;
        default:
            NETCO_DEBUG_PRINT(TEXT("PreprocessCommand returned: ") << dwPreprocessResult);
            return dwPreprocessResult;
        }
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

//
// Usage: restart [idx=]0-N
//
// Parameters:
//
//      IDX - Specifies the device index as it is shown in "show devices" output.
//
// Remarks:
//
//      Restarts device specified by index.
//
// Examples:
//
//      restart idx=0
//      restart 2
//
DWORD WINAPI _NetKVMRestartDeviceCmdHandler(__in   PWCHAR  /*pwszMachine*/,
                                            __in   PWCHAR* ppwcArguments,
                                            __in   DWORD   dwCurrentIndex,
                                            __in   DWORD   dwArgCount,
                                            __in   DWORD   /*dwFlags*/,
                                            __in   PVOID   /*pvData*/,
                                            __out  BOOL*   pbDone)
{
    *pbDone = FALSE; /* Just to make static analyzer happy */

    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMRestartDeviceCmdHandler called"));
        TAG_TYPE TagsList[] =
            { {NETKVM_IDX_PARAM_NAME,   NS_REQ_PRESENT} };

        auto_ptr<DWORD> pdwTagMatchResults(new DWORD[dwArgCount - dwCurrentIndex]);
        DWORD dwPreprocessResult = PreprocessCommand(NULL, ppwcArguments,
                                                     dwCurrentIndex, dwArgCount,
                                                     TagsList, ARRAY_SIZE(TagsList),
                                                     ARRAY_SIZE(TagsList), ARRAY_SIZE(TagsList),
                                                     pdwTagMatchResults.get());

        switch (dwPreprocessResult)
        {
        case NO_ERROR:
            {
                DWORD dwIndex;
                if(__NetKVMConvertDeviceIndex(ppwcArguments[dwCurrentIndex + pdwTagMatchResults.get()[0]], &dwIndex))
                {
                    PrintMessageFromModule(g_hinstThisDLL, IDS_RESTARTINGDEVICE);
                    tcout << TEXT(" ") << dwIndex << TEXT("... ");
                    DWORD dwError = _NetKVMRestartDevice(dwIndex);
                    switch(dwError)
                    {
                    case NO_ERROR:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_DONE);
                        break;
                    case ERROR_SUCCESS_REBOOT_REQUIRED:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_REBOOTREQUIRED);
                        break;
                    default:
                        PrintMessageFromModule(g_hinstThisDLL, IDS_FAIL);
                        break;
                    }
                    tcout << endl;
                    return dwError;
                }
                else
                {
                    return ERROR_INVALID_PARAMETER;
                }
            }
            __fallthrough;
        default:
            NETCO_DEBUG_PRINT(TEXT("PreprocessCommand returned: ") << dwPreprocessResult);
            return dwPreprocessResult;
        }
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

#define CMD_NETKVM_SHOW_DEVICES       L"devices"
#define HLP_NETKVM_SHOW_DEVICES       IDS_SHOWDEVICESSHORT
#define HLP_NETKVM_SHOW_DEVICES_EX    IDS_SHOWDEVICESLONG
#define CMD_NETKVM_SHOW_PARAMS        L"parameters"
#define HLP_NETKVM_SHOW_PARAMS        IDS_SHOWPARAMSSHORT
#define HLP_NETKVM_SHOW_PARAMS_EX     IDS_SHOWPARAMSLONG
#define CMD_NETKVM_SHOW_PARAMINFO     L"paraminfo"
#define HLP_NETKVM_SHOW_PARAMINFO     IDS_SHOWPARAMINFOSHORT
#define HLP_NETKVM_SHOW_PARAMINFO_EX  IDS_SHOWPARAMINFOLONG

CMD_ENTRY  g_ShowCmdTable[] =
{
    CREATE_CMD_ENTRY_EX(NETKVM_SHOW_DEVICES,
                        (PFN_HANDLE_CMD) _NetKVMShowDevicesCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL),
    CREATE_CMD_ENTRY_EX(NETKVM_SHOW_PARAMS,
                        (PFN_HANDLE_CMD) _NetKVMShowParamsCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL),
    CREATE_CMD_ENTRY_EX(NETKVM_SHOW_PARAMINFO,
                        (PFN_HANDLE_CMD) _NetKVMShowParamInfoCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL)

};

#define CMD_NETKVM_RESTART_DEVICE       L"restart"
#define CMD_NETKVM_RESTART_DEVICE_T     TEXT("restart")
#define HLP_NETKVM_RESTART_DEVICE       IDS_RESTARTDEVICE
#define HLP_NETKVM_RESTART_DEVICE_EX    IDS_RESTARTDEVICELONG
#define CMD_NETKVM_GET_PARAM            L"getparam"
#define HLP_NETKVM_GET_PARAM            IDS_GETPARAM
#define HLP_NETKVM_GET_PARAM_EX         IDS_GETPARAMLONG
#define CMD_NETKVM_SET_PARAM            L"setparam"
#define CMD_NETKVM_SET_PARAM_T          TEXT("setparam")
#define HLP_NETKVM_SET_PARAM            IDS_SETPARAM
#define HLP_NETKVM_SET_PARAM_EX         IDS_SETPARAMLONG

CMD_ENTRY  g_TopLevelCommands[] =
{
    CREATE_CMD_ENTRY_EX(NETKVM_RESTART_DEVICE,
                        (PFN_HANDLE_CMD) _NetKVMRestartDeviceCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL),
    CREATE_CMD_ENTRY_EX(NETKVM_GET_PARAM,
                        (PFN_HANDLE_CMD) _NetKVMGetParamCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL),
    CREATE_CMD_ENTRY_EX(NETKVM_SET_PARAM,
                        (PFN_HANDLE_CMD) _NetKVMSetParamCmdHandler,
                        CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL)
};


#define HLP_GROUP_SHOW       IDS_SHOWCMDHELP
#define CMD_GROUP_SHOW       L"show"

static CMD_GROUP_ENTRY g_TopLevelGroups[] =
{
    CREATE_CMD_GROUP_ENTRY_EX(GROUP_SHOW, g_ShowCmdTable, CMD_FLAG_PRIVATE | CMD_FLAG_LOCAL)
};

DWORD WINAPI _NetKVMDumpCdmHandler(__in  PWCHAR      pwszRouter,
                                   __in  WCHAR ** /* ppwcArguments */,
                                   __in  DWORD    /* dwArgCount */,
                                   __in  PVOID    /* pvData */)
{
    try
    {
        NETCO_DEBUG_PRINT(TEXT("_NetKVMDumpCdmHandler called"));

        if(NULL != pwszRouter)
        {
            PrintError(g_hinstThisDLL, IDS_LOCALONLY);
            return ERROR_INVALID_PARAMETER;
        }

        tcout << endl<< TEXT("pushd ") << NETKVM_HELPER_NAME << endl << endl;

        for(DWORD i = 0; i < g_DevicesOfInterest.size(); i++)
        {
            ParamValueListT ValuesList;
            _NetKVMFillDeviceValuesList(i, ValuesList);

            for(ParamValueListT::const_iterator it = ValuesList.begin();
                it != ValuesList.end(); ++it)
            {
                tcout << CMD_NETKVM_SET_PARAM_T << TEXT(" ")
                      << NETKVM_IDX_PARAM_NAME_T << TEXT("=") << i << TEXT(" ")
                      << NETKVM_PARAM_PARAM_NAME_T << TEXT("=") << it->first << TEXT(" ")
                      << NETKVM_VALUE_PARAM_NAME_T << TEXT("=") << it->second << endl;
            }

            tcout << CMD_NETKVM_RESTART_DEVICE_T << TEXT(" ")
                  << NETKVM_IDX_PARAM_NAME_T << TEXT("=") << i << TEXT(" ")
                  << endl << endl;
        }

        tcout << TEXT("popd") << endl;

        return NO_ERROR;
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }
}

DWORD WINAPI _NetKVMNetshStartHelper(__in  const GUID *pguidParent,
                                     __in  DWORD dwVersion)
{
    try
    {
        UNREFERENCED_PARAMETER(pguidParent);
        UNREFERENCED_PARAMETER(dwVersion);

        NETCO_DEBUG_PRINT(TEXT("_NetKVMNetshStartHelper called"));

        pair< HDEVINFO, vector<_NetKVMDeviceInfo> > Devices = _NetKVMGetDevicesOfInterest();
        g_hDeviceInfoList = Devices.first;
        g_DevicesOfInterest = Devices.second;

        if(g_DevicesOfInterest.empty())
        {
            PrintMessageFromModule(g_hinstThisDLL, IDS_NODEVICESFOUND);
            return NO_ERROR;
        }

        NS_CONTEXT_ATTRIBUTES attr;
        ZeroMemory(&attr, sizeof(attr));
        attr.dwVersion = NETKVM_HELPER_VERSION;
        attr.dwReserved = 0;
        attr.pwszContext = NETKVM_HELPER_NAME_W;
        attr.guidHelper = NETKVM_HELPER_GUID;
        attr.dwFlags = CMD_FLAG_LOCAL;
        attr.ulPriority = DEFAULT_CONTEXT_PRIORITY;
        attr.ulNumTopCmds = ARRAYSIZE(g_TopLevelCommands);
        attr.pTopCmds = (CMD_ENTRY (*)[])g_TopLevelCommands;
        attr.ulNumGroups = ARRAYSIZE(g_TopLevelGroups);
        attr.pCmdGroups = (CMD_GROUP_ENTRY (*)[])g_TopLevelGroups;
        attr.pfnCommitFn = NULL;
        attr.pfnDumpFn = (PNS_CONTEXT_DUMP_FN) _NetKVMDumpCdmHandler;
        attr.pfnConnectFn = NULL;
        attr.pReserved = NULL;
        RegisterContext(&attr);
    }
    catch(const exception& ex)
    {
        PrintError(g_hinstThisDLL, IDS_LOGICEXCEPTION);
        tcout << TEXT(": ") << string2tstring(string(ex.what())) << endl;
        return ERROR_EXCEPTION_IN_SERVICE;
    }
    catch(...)
    {
        return ERROR_UNKNOWN_EXCEPTION;
    }

    return NO_ERROR;
}

DWORD WINAPI _NetKVMNetshStopHelper(__in  DWORD dwReserved)
{
    UNREFERENCED_PARAMETER(dwReserved);

    NETCO_DEBUG_PRINT(TEXT("_NetKVMNetshStopHelper called"));

    return SetupDiDestroyDeviceInfoList(g_hDeviceInfoList) ? NO_ERROR
                                                           : GetLastError();
}

DWORD NETCO_API InitHelperDll(__in DWORD dwNetshVersion,
                                   PVOID pReserved)
{
    UNREFERENCED_PARAMETER(dwNetshVersion);
    UNREFERENCED_PARAMETER(pReserved);

    NS_HELPER_ATTRIBUTES attr;

    ZeroMemory(&attr, sizeof(attr));
    attr.guidHelper = NETKVM_HELPER_GUID;
    attr.dwVersion  = NETKVM_HELPER_VERSION;
    attr.pfnStart   = _NetKVMNetshStartHelper;
    attr.pfnStop    = _NetKVMNetshStopHelper;
    RegisterHelper( NULL, &attr );

    return NO_ERROR;
}

static DWORD _GetThisDLLPathName(LPTSTR szPathName, DWORD *pdwLength)
{
    DWORD dwPathLength = GetModuleFileName(g_hinstThisDLL, szPathName, *pdwLength);
    DWORD dwErr = GetLastError();
    if(ERROR_SUCCESS != dwErr)
    {
        NETCO_DEBUG_PRINT(TEXT("GetModuleFileName failed. Error code: ") << dwErr);
        return dwErr;
    }
    else if(dwPathLength == *pdwLength)
    {
        NETCO_DEBUG_PRINT(TEXT("Buffer provided to GetModuleFileName is too small"));
        return ERROR_BUFFER_OVERFLOW;
    }
    else
    {
        NETCO_DEBUG_PRINT(TEXT("DLL pathname: ") << szPathName);
        *pdwLength = dwPathLength;
        return ERROR_SUCCESS;
    }
}

static const LPCTSTR NETSH_HELPERS_LIST_PATH = TEXT("SOFTWARE\\Microsoft\\NetSh");
static const HKEY    NETSH_HELPERS_LIST_HIVE = HKEY_LOCAL_MACHINE;

DWORD NETCO_API RegisterNetKVMNetShHelper(void)
{
    try
    {
        TCHAR szDllPathName[MAX_PATH];
        DWORD dwPathNameLength = TBUF_SIZEOF(szDllPathName);

        DWORD dwErr = _GetThisDLLPathName(szDllPathName, &dwPathNameLength);
        if(ERROR_SUCCESS != dwErr)
        {
            tstringstream strmError;
            strmError << TEXT("_GetThisDLLPathName failed with code ") << dwErr;
            OutputDebugString(strmError.str().c_str());
            return dwErr;
        }

        neTKVMRegAccess regAccess(NETSH_HELPERS_LIST_HIVE, NETSH_HELPERS_LIST_PATH);
        if(!regAccess.WriteString(NETKVM_HELPER_NAME, szDllPathName))
        {
            dwErr = GetLastError();
            tstringstream strmError;
            strmError << TEXT("Registry operation failed with code ") << dwErr;
            OutputDebugString(strmError.str().c_str());
            return dwErr;
        }
        return ERROR_SUCCESS;
    }
    catch (const exception& ex)
    {
        OutputDebugStringA(ex.what());
        return ERROR_INSTALL_FAILURE;
    }
}
DWORD NETCO_API UnregisterNetKVMNetShHelper(void)
{
    try
    {
        neTKVMRegAccess regAccess(NETSH_HELPERS_LIST_HIVE, NETSH_HELPERS_LIST_PATH);
        regAccess.DeleteValue(NETKVM_HELPER_NAME);
        return ERROR_SUCCESS;
    }
    catch (const exception& ex)
    {
        OutputDebugStringA(ex.what());
        return ERROR_INSTALL_FAILURE;
    }
}

BOOL NETCO_API __stdcall DllMain(__in  HINSTANCE hinstDLL,
                                 __in  DWORD fdwReason,
                                 __in  LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(lpvReserved);

    NETCO_DEBUG_PRINT(TEXT("DllMain(") << fdwReason << TEXT(") called"));

    //Obtain DLL path name and store it for future use
    if(DLL_PROCESS_ATTACH == fdwReason)
    {
        g_hinstThisDLL = hinstDLL;
    }

    return TRUE;
}
