#include "stdafx.h"
#include <cfg.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <devguid.h>
#include <devpkey.h>

#pragma comment( lib , "setupapi.lib")
#pragma comment( lib , "cfgmgr32.lib")

class CDeviceInfoSet
{
public:
    CDeviceInfoSet()
    {
        m_Devinfo = SetupDiGetClassDevsEx(&GUID_DEVCLASS_NET,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr);
        if (m_Devinfo == INVALID_HANDLE_VALUE)
            m_Devinfo = NULL;
        else
            Enumerate();
    }
    ~CDeviceInfoSet()
    {
        if (m_Devinfo)
            SetupDiDestroyDeviceInfoList(m_Devinfo);
    }
    class CDeviceInfo
    {
    public:
        CDeviceInfo(HDEVINFO DevInfoSet, SP_DEVINFO_DATA& Data)
        {
            m_Devinfo = DevInfoSet;
            m_Data = Data;
            WCHAR buffer[256];
            DWORD bufferSize = ARRAYSIZE(buffer);
            if (SetupDiGetDeviceInstanceId(
                m_Devinfo, &m_Data,
                buffer,
                bufferSize,
                &bufferSize))
            {
                m_DevInstId = buffer;
                Log("Found %S", buffer);
                Log("\tDescription: %S", Description().GetString());
                Log("\tFriendly name: %S", FriendlyName().GetString());
                Log("\tStatus: %08X", Status());
            }
        }
        const CString& Id() const { return m_DevInstId; }
        CString Description()
        {
            return GetDeviceStringProperty(DEVPKEY_Device_DeviceDesc);
        }
        CString FriendlyName()
        {
            return GetDeviceStringProperty(DEVPKEY_Device_FriendlyName);
        }
        UINT Status()
        {
            return GetDeviceUlongProperty(DEVPKEY_Device_DevNodeStatus);
        }
        void Restart()
        {
            CallClass(DICS_PROPCHANGE);
        }
    protected:
        SP_DEVINFO_DATA m_Data;
        HDEVINFO        m_Devinfo;
        CString         m_DevInstId;
        CString GetDeviceStringProperty(const DEVPROPKEY &PropKey)
        {
            BYTE buffer[512];
            DWORD bufferSize = sizeof(buffer);
            DEVPROPTYPE propType;
            CString s;

            if (SetupDiGetDeviceProperty(m_Devinfo, &m_Data, &PropKey, &propType, buffer, bufferSize, &bufferSize, 0) &&
                propType == DEVPROP_TYPE_STRING)
            {
                s = (LPCWSTR)buffer;
            }
            return s;
        }
        ULONG GetDeviceUlongProperty(const DEVPROPKEY &PropKey)
        {
            ULONG val = 0;
            DWORD bufferSize = sizeof(val);
            DEVPROPTYPE propType;

            if (!SetupDiGetDeviceProperty(m_Devinfo, &m_Data, &PropKey, &propType, (BYTE *)&val, bufferSize, &bufferSize, 0) ||
                propType == DEVPROP_TYPE_UINT32)
            {
                //error
            }
            return val;
        }
        void CallClass(ULONG Code)
        {
            SP_PROPCHANGE_PARAMS pcp;
            pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            pcp.StateChange = Code;
            pcp.Scope = DICS_FLAG_CONFIGSPECIFIC;
            pcp.HwProfile = 0;
            if (!SetupDiSetClassInstallParams(m_Devinfo, &m_Data, &pcp.ClassInstallHeader, sizeof(pcp)) ||
                !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, m_Devinfo, &m_Data))
            {
                Log("%s failed for code %d, error %d", __FUNCTION__, Code, GetLastError());
            }
            else {
                Log("%s succeeded code %d", __FUNCTION__, Code);
            }
        }
    };
    void Enumerate()
    {
        if (!m_Devinfo)
            return;
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof(devInfo);
        for (ULONG i = 0; SetupDiEnumDeviceInfo(m_Devinfo, i, &devInfo); i++)
        {
            CDeviceInfo dev(m_Devinfo, devInfo);
            m_Devices.Add(dev);
        }
    }
    CDeviceInfo *Find(LPCWSTR FriendlyName)
    {
        for (UINT i = 0; i < m_Devices.GetCount(); ++i)
        {
            if (!m_Devices[i].FriendlyName().Compare(FriendlyName))
                return &m_Devices[i];
        }
        return NULL;
    }
protected:
    HDEVINFO m_Devinfo;
    CAtlArray<CDeviceInfo> m_Devices;
};

void ProcessProtocolUninstall()
{
    CWmicQueryRunner runner;
    CAtlArray<CString>& devices = runner.Devices();

    int ret = system("wmic.exe /namespace:\\\\root\\wmi path NetKvm_Standby set value=0");
    Log("wmic set returned %d", ret);
    runner.Run();
    if (!devices.GetCount())
        return;
    CDeviceInfoSet pnpTree;
    for (UINT i = 0; i < devices.GetCount(); ++i)
    {
        auto device = pnpTree.Find(devices[i]);
        if (device)
        {
            Log("found %S\n", device->Id().GetString());
            device->Restart();
        }
        else
        {
            Log("not found %S\n", devices[i].GetString());
        }
    }
}
