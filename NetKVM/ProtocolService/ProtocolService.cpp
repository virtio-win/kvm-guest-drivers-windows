/*
 * This file contains implementation of minimal user-mode service
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "stdafx.h"

/*
    The protocol service replaces and extends the notification object.
    Unlike previous implementation when the VIOPROT was enabled only
    on especially defined set of adapters, now the VIOPROT is installed and
    enabled on all the ethernet adapters. The service is responsible for:
    - disable all other protocols/filters only when needed (when the adapter
      has the same MAC as virtio-net adapter)
    - enable all other protocols when the protocol is uninstalled
    - make standby netkvm adapter functional after several seconds if the VF
      is not present and reconnect via VF when the VF comes
    - enable all other protocols on VF when the netkvm is disabled
*/

class CNetCfg
{
public:
    CNetCfg()
    {
        hr = CoCreateInstance(CLSID_CNetCfg, NULL, CLSCTX_SERVER, IID_INetCfg, (LPVOID*)&m_NetCfg);
        if (!m_NetCfg) {
            return;
        }
        hr = m_NetCfg->QueryInterface(IID_INetCfgLock, (LPVOID*)&m_NetCfgLock);
        if (!m_NetCfgLock) {
            return;
        }
        m_Usable = m_NetCfgLock->AcquireWriteLock(5000, L"netkvmp service", NULL) == S_OK;
        if (m_Usable) {
            hr = m_NetCfg->Initialize(NULL);
            if (hr != S_OK) {
                m_NetCfgLock->ReleaseWriteLock();
                m_Usable = false;
            }
        }
    }
    ~CNetCfg()
    {
        if (!m_Usable) {
            return;
        }
        if (m_Modified)
        {
            Log("Applying binding changes");
            m_NetCfg->Apply();
        }
        m_NetCfgLock->ReleaseWriteLock();
        m_NetCfg->Uninitialize();
    }
    bool Usable() const { return m_Usable; }
    void EnableVioProtOnly(const CString& Name, bool Enable)
    {
        CString sVioProt = L"vioprot";
        CComPtr<INetCfgClass> netClass;
        hr = m_NetCfg->QueryNetCfgClass(&GUID_DEVCLASS_NET, IID_INetCfgClass, (LPVOID*)&netClass);
        if (hr != S_OK) {
            return;
        }
        CComPtr<IEnumNetCfgComponent> netEnum;
        hr = netClass->EnumComponents(&netEnum);
        if (hr != S_OK) {
            return;
        }
        do {
            CComPtr<INetCfgComponent> adapter;
            hr = netEnum->Next(1, &adapter, NULL);
            if (hr != S_OK) {
                break;
            }
            LPWSTR id = NULL;
            hr = adapter->GetDisplayName(&id);
            if (hr != S_OK) {
                break;
            }
            bool found = !Name.CompareNoCase(id);
            CoTaskMemFree(id);
            if (found) {
                Log("found %S", Name.GetString());
                CComPtr<INetCfgComponentBindings> bindings;
                CComPtr<IEnumNetCfgBindingPath> paths;
                hr = adapter->QueryInterface(IID_INetCfgComponentBindings, (LPVOID*)&bindings);
                if (hr != S_OK) {
                    return;
                }
                hr = bindings->EnumBindingPaths(EBP_ABOVE, &paths);
                if (!paths) {
                    return;
                }
                do
                {
                    CComPtr<INetCfgBindingPath> path;
                    hr = paths->Next(1, &path, NULL);
                    if (hr != S_OK) {
                        break;
                    }
                    bool enabled = path->IsEnabled() == S_OK;
                    CComPtr<IEnumNetCfgBindingInterface> enumBindingIf;
                    hr = path->EnumBindingInterfaces(&enumBindingIf);
                    if (hr == S_OK) {
                        CComPtr<INetCfgBindingInterface> bindingIf;
                        hr = enumBindingIf->Next(1, &bindingIf, NULL);
                        if (hr == S_OK)
                        {
                            LPWSTR upperId = NULL, lowerId = NULL;
                            CComPtr<INetCfgComponent> upper, lower;
                            bindingIf->GetUpperComponent(&upper);
                            bindingIf->GetLowerComponent(&lower);
                            if (upper) upper->GetId(&upperId);
                            if (lower) lower->GetId(&lowerId);
                            if (upperId && lowerId && lower == adapter)
                            {
                                bool bIsVioProt = !sVioProt.CompareNoCase(upperId);
                                // vioprot should be enabled always, all the rest - if 'Enable{OnlyVioProt}==false'
                                bool bShouldBeEnabled = !Enable || bIsVioProt;
                                Log("%sabled U:%S L:%S (should be %sabled)",
                                    enabled ? "en" : "dis", upperId, lowerId,
                                    bShouldBeEnabled ? "en" : "dis");
                                if (bShouldBeEnabled != enabled)
                                {
                                    hr = path->Enable(bShouldBeEnabled);
                                    if (hr != S_OK)
                                    {
                                        Log("Can't %sable hr=%X", bShouldBeEnabled ? "en" : "dis", hr);
                                    }
                                    else
                                    {
                                        m_Modified = true;
                                    }
                                }
                            }
                        }
                    }

                } while (true);

                break;
            }

        } while (true);

    }
private:
    CComPtr<INetCfg> m_NetCfg;
    CComPtr<INetCfgLock> m_NetCfgLock;
    bool m_Usable = false;
    bool m_Modified = false;
    HRESULT hr;
};

class CMACString
{
public:
    CMACString(const UCHAR* Address)
    {
        const char chars[] = "0123456789ABCDEF";
        for (int i = 0; i < 6; ++i)
        {
            UCHAR c = Address[i];
            m_Buffer[i * 3] = chars[c >> 4];
            m_Buffer[i * 3 + 1] = chars[c & 0x0f];
            m_Buffer[i * 3 + 2] = ':';
        }
        m_Buffer[sizeof(m_Buffer) - 1] = 0;
    }
    const char* Get() { return m_Buffer; }
private:
    char m_Buffer[6 * 3] = {};
};

// we can't use GetAdaptersTable
// as it does not allow to enumerate adapters
// when both ms_tcpip and ms_tcpip6 are disabled
class CInterfaceTable
{
public:
    CInterfaceTable()
    {
        GetIfTable2(&m_Table);
    }
    void CheckBinding(ULONG index, bool OnlyVioProt)
    {
        CNetCfg cfg;
        for (ULONG i = 0; m_Table && i < m_Table->NumEntries; ++i)
        {
            auto& row = m_Table->Table[i];
            if (row.Type == IF_TYPE_ETHERNET_CSMACD &&
                row.MediaConnectState == MediaConnectStateConnected &&
                !row.InterfaceAndOperStatusFlags.FilterInterface &&
                !row.InterfaceAndOperStatusFlags.NotMediaConnected)
            {
                // Description - adapter name
                // Alias - connection name
                Log("IF %d (%S)(%S)", row.InterfaceIndex, row.Description, row.Alias);
                if (index == INFINITE || index == row.InterfaceIndex)
                {
                    cfg.EnableVioProtOnly(row.Description, OnlyVioProt);
                }
            }
        }
    }
    ~CInterfaceTable()
    {
        FreeMibTable(m_Table);
    }
private:
    PMIB_IF_TABLE2 m_Table = NULL;
};

class CDeviceNotificationOwner
{
public:
    CDeviceNotificationOwner() {}
    // return false only on CM_NOTIFY_ACTION_DEVICEQUERYREMOVE and only when needed
    virtual bool Notification(CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD dataSize) = 0;
};

class CDeviceNotification
{
public:
    CDeviceNotification(CDeviceNotificationOwner& owner) :
        m_Owner(owner)
    {
    }
    bool Register(CM_NOTIFY_FILTER* filter)
    {
        CONFIGRET cr = CM_Register_Notification(filter, this,
            [](HCMNOTIFICATION h, PVOID Context, CM_NOTIFY_ACTION Action, PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize) -> DWORD
            {
                CDeviceNotification* obj = (CDeviceNotification*)Context;
                DWORD res = obj->Notification(Action, EventData, EventDataSize) ? ERROR_SUCCESS : ERROR_CANCELLED;
                if (res != ERROR_SUCCESS)
                {
                    Log("WARNING: returning %d from PnP notification", res);
                    UNREFERENCED_PARAMETER(h);
                }
                return res;
            },
            &m_Notification);
        if (!m_Notification)
        {
            Log("%s: failed to register, cr %d", __FUNCTION__, cr);
        }
        return m_Notification != NULL;
    }
    ~CDeviceNotification()
    {
        if (m_Notification)
        {
            CM_Unregister_Notification(m_Notification);
        }
        m_Notification = NULL;
    }
    bool Notification(CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD dataSize)
    {
        Log("%s: action %s", __FUNCTION__, Name<CM_NOTIFY_ACTION>(action));
        return m_Owner.Notification(action, data, dataSize);
    }
private:
    HCMNOTIFICATION m_Notification = NULL;
    CDeviceNotificationOwner& m_Owner;
};

class CNetworkDeviceNotification : public CDeviceNotification
{
public:
    CNetworkDeviceNotification(CDeviceNotificationOwner& owner) :
        CDeviceNotification(owner)
    {
        m_Filter.cbSize = sizeof(m_Filter);
        m_Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        m_Filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_NET;
        Register(&m_Filter);
    }
    CM_NOTIFY_FILTER m_Filter;
};

class CNetkvmDeviceFile
{
public:
    CNetkvmDeviceFile()
    {
        LPCWSTR devName = L"\\\\.\\" NETKVM_DEVICE_NAME;
        ULONG access = GENERIC_READ | GENERIC_WRITE, share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        m_Handle = CreateFile(devName, access, share,
            NULL, OPEN_EXISTING, 0, NULL);
        if (m_Handle == INVALID_HANDLE_VALUE) {
            //Log("can't open %S(%d)\n", devName, GetLastError());
            m_Handle = NULL;
        }
    }
    ~CNetkvmDeviceFile()
    {
        if (m_Handle) {
            CloseHandle(m_Handle);
        }
    }
    bool ControlGet(ULONG Code, PVOID InBuf, ULONG InSize)
    {
        return Control(Code, NULL, 0, InBuf, InSize);
    }
    bool ControlSet(ULONG Code, PVOID OutBuf, ULONG OutSize)
    {
        return Control(Code, OutBuf, OutSize, NULL, 0);
    }
    bool Control(ULONG Code, PVOID InBuf, ULONG InSize, PVOID OutBuf, ULONG OutSize)
    {
        return DeviceIoControl(m_Handle, Code, InBuf, InSize, OutBuf, OutSize, &m_Returned, NULL);
    }
    bool Usable() const { return m_Handle; }
    ULONG Returned() const { return m_Returned; }
protected:
    HANDLE m_Handle;
    ULONG m_Returned = 0;
};

static void CheckBinding(ULONG index, bool OnlyVioProt)
{
    CInterfaceTable t;
    t.CheckBinding(index, OnlyVioProt);
}

class CVirtioAdapter
{
public:
    CVirtioAdapter(UCHAR *Mac = NULL)
    {
        if (Mac)
        {
            RtlCopyMemory(m_MacAddress, Mac, sizeof(m_MacAddress));
        }
    }
    ULONG m_Count = 0;
    typedef enum _tAction { acNone, acOn, acOff } tAction;
    tAdapterState m_State = asUnknown;
    UCHAR m_MacAddress[6];
    ULONG m_VfIndex = INFINITE;
    bool Match(UCHAR* Mac)
    {
        return !memcmp(m_MacAddress, Mac, sizeof(m_MacAddress));
    }
    void SetState(tAdapterState State, const NETKVMD_ADAPTER& a)
    {
        tAction action = acNone;
        m_Count = 0;
        switch (State)
        {
            case asStandalone:
                // if SuppressLink or Started is set - this is our error
                // if VF present - probably we need to unbind it from everything
                break;
            case asAloneInactive:
                // virtio and standby are on, no vf, virtio link is off
                // if Suppressed is set, better to clear it
                if (a.SuppressLink) action = acOn;
                break;
            case asAloneActive:
                // virtio, standby, virtio link are on, no vf
                // if Suppressed is set, must clear it
                if (a.SuppressLink) action = acOn;
                break;
            case asBoundInactive:
                // virtio, standby, vf, but virtio link is off
                // wait for carrier
                // if suppressed is set, need to clear it
                if (a.SuppressLink) action = acOn;
                break;
            case asBoundInitial:
                // virtio, standby, vf, suppress, not started
                // for example after netkvm parameters change
                action = acOn;
                CheckBinding(a.VfIfIndex, true);
                break;
            case asBoundActive:
                // working failover
                if (!a.SuppressLink && !a.Started)
                {
                    // VF comes when virtio becomes active after initial timeout
                    action = acOff;
                    m_Count = INFINITE;
                    CheckBinding(a.VfIfIndex, true);
                }
                break;
            case asAbsent:
                // working VF without virtio
                // VF should be bound to all the default protocols (virtio is optional)
                CheckBinding(a.VfIfIndex, false);
                break;
            default:
                break;
        }
        m_State = State;
        NETKVMD_SET_LINK sl;
        RtlCopyMemory(sl.MacAddress, m_MacAddress, sizeof(m_MacAddress));
        sl.LinkOn = action == acOn ? 1 : 0;
        if (action == acOn || action == acOff)
        {
            CNetkvmDeviceFile d;
            d.ControlSet(IOCTL_NETKVMD_SET_LINK, &sl, sizeof(sl));
        }
    }
    void Update(const NETKVMD_ADAPTER& a)
    {
        ULONG index = 0;
        CMACString s(a.MacAddress);

        // here we convert seven booleans to 7-bit index
        // state is determined as m_TargetStates[index]
        if (a.Virtio) index |= 1;
        if (a.IsStandby) index |= 2;
        if (a.VirtioLink) index |= 4;
        if (a.SuppressLink) index |= 8;
        if (a.Started) index |= 16;
        if (a.HasVf)
        {
            index |= 32;
            m_VfIndex = a.VfIfIndex;
        }
        else
        {
            m_VfIndex = INFINITE;
        }
        if (a.VfLink) index |= 64;
        tAdapterState State = m_TargetStates[index];
        if (State != m_State || m_Count == INFINITE)
        {
            Log("[%s] v%d, sb%d, l%d, su%d, st%d, vf%d, vfidx %d, vl%d",
                s.Get(), a.Virtio, a.IsStandby, a.VirtioLink, a.SuppressLink, a.Started, a.HasVf, a.VfIfIndex, a.VfLink);
            Log("[%s] %s => %s", s.Get(), Name<tAdapterState>(m_State), Name<tAdapterState>(State));
            SetState(State, a);
        }
        else
        {
            m_Count++;
        }
    }
    void Dump()
    {
        CMACString s(m_MacAddress);
        Log("[%s] %s vfIdx %d", s.Get(), Name<tAdapterState>(m_State), m_VfIndex);
    }
    void PreRemove()
    {
        if ((m_State == asBoundInactive || m_State == asBoundActive) &&
            m_VfIndex != INFINITE)
        {
            CheckBinding(m_VfIndex, false);
        }
    }
private:
    static const tAdapterState CVirtioAdapter::m_TargetStates[];
};

const tAdapterState CVirtioAdapter::m_TargetStates[128] =
{
    /* 0:  Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 1:  Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asStandalone,
    /* 2:  Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 3:  Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asAloneInactive,
    /* 4:  Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 5:  Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asStandalone,
    /* 6:  Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 7:  Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 0 */ asAloneActive,
    /* 8:  Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 9:  Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asStandalone,
    /* 10: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 11: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asAloneInactive,
    /* 12: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 13: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asStandalone,
    /* 14: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asUnknown,
    /* 15: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 0 */ asAloneActive,
    /* 16: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 17: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asStandalone,
    /* 18: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 19: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asAloneInactive,
    /* 20: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 21: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asStandalone,
    /* 22: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 23: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 0 */ asAloneActive,
    /* 24: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 25: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asStandalone,
    /* 26: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 27: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asAloneInactive,
    /* 28: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 29: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asStandalone,
    /* 30: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asUnknown,
    /* 31: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 0 */ asAloneActive,
    /* 32: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asAbsent,
    /* 33: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asStandalone,
    /* 34: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 35: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 36: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 37: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asStandalone,
    /* 38: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 39: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 40: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 41: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asStandalone,
    /* 42: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 43: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 44: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 45: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asStandalone,
    /* 46: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asUnknown,
    /* 47: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 48: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 49: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asStandalone,
    /* 50: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 51: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 52: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 53: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asStandalone,
    /* 54: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 55: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 56: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 57: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asStandalone,
    /* 58: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 59: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asAloneInactive,
    /* 60: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 61: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asStandalone,
    /* 62: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asUnknown,
    /* 63: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 0 */ asBoundInactive,
    /* 64:  Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 65:  Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 66:  Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 67:  Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 68:  Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 69:  Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 70:  Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 71:  Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 72:  Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 73:  Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 74: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 75: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 76: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 77: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 78: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 79: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 0, VfLink 1 */ asUnknown,
    /* 80: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 81: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 82: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 83: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 84: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 85: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 86: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 87: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 88: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 89: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 90: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 91: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 92: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 93: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 94: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 95: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 0, VfLink 1 */ asUnknown,
    /* 96: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asAbsent,
    /* 97: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asStandalone,
    /* 98: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 99: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asBoundInactive,
    /* 100: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 101: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asStandalone,
    /* 102: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 103: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 0, HasVf 1, VfLink 1 */ asBoundActive,
    /* 104: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 105: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asStandalone,
    /* 106: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 107: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asBoundInactive,
    /* 108: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 109: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asStandalone,
    /* 110: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asUnknown,
    /* 111: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 0, HasVf 1, VfLink 1 */ asBoundInitial,
    /* 112: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asAbsent,
    /* 113: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asStandalone,
    /* 114: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 115: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asBoundInactive,
    /* 116: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 117: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asStandalone,
    /* 118: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 119: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 0, Started 1, HasVf 1, VfLink 1 */ asBoundActive,
    /* 120: Virtio 0, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 121: Virtio 1, IsStandby 0, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asStandalone,
    /* 122: Virtio 0, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 123: Virtio 1, IsStandby 1, VirtioLink 0, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asBoundInactive,
    /* 124: Virtio 0, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 125: Virtio 1, IsStandby 0, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asStandalone,
    /* 126: Virtio 0, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asUnknown,
    /* 127: Virtio 1, IsStandby 1, VirtioLink 1, SuppressLink 1, Started 1, HasVf 1, VfLink 1 */ asBoundActive,
};

class CVirtioAdaptersArray : public CAtlArray<CVirtioAdapter>
{
public:
    void RemoveAdapter(UINT Index)
    {
        CVirtioAdapter& a = GetAt(Index);
        a.PreRemove();
        RemoveAt(Index);
    }
    void RemoveAllAdapters()
    {
        for (UINT i = 0; i < GetCount(); ++i)
        {
            CVirtioAdapter& a = GetAt(i);
            a.PreRemove();
        }
        RemoveAll();
    }
};

class CFileFinder
{
public:
    CFileFinder(const CString& WildCard) : m_WildCard(WildCard) {};
    template<typename T> bool Process(T Functor)
    {
        HANDLE h = FindFirstFile(m_WildCard, &m_fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        while (Functor(m_fd.cFileName) && FindNextFile(h, &m_fd)) {}
        FindClose(h);
        return true;
    }
private:
    WIN32_FIND_DATA m_fd = {};
    const CString& m_WildCard;
};

class CInfDirectory : public CString
{
public:
    CInfDirectory()
    {
        WCHAR* p = new WCHAR[MAX_PATH];
        if (p)
        {
            if (GetWindowsDirectory(p, MAX_PATH))
            {
                Append(p);
                Append(L"\\INF\\");
            }
            delete[] p;
        }
    }
};

class CProtocolServiceImplementation :
    public CServiceImplementation,
    public CThreadOwner
{
public:
    CProtocolServiceImplementation() :
        CServiceImplementation(_T("netkvmp")),
        m_ThreadEvent(false)
        {}
    enum { ctlDump = 128 };
protected:
    virtual bool OnStart() override
    {
        StartThread();
        return true;
    }
    virtual DWORD ControlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData)
    {
        DWORD res = NO_ERROR;
        switch (dwControl)
        {
            case ctlDump:
                Dump();
                return res;
            default:
                break;
        }
        res = __super::ControlHandler(dwControl, dwEventType, lpEventData);
        return res;
    }
    virtual bool OnStop() override
    {
        StopThread();
        m_ThreadEvent.Set();
        return true;
    }
    virtual void ThreadProc()
    {
        CoInitialize(NULL);

        do {
            if (ThreadState() != tsRunning)
                break;
            SyncVirtioAdapters();
            m_ThreadEvent.Wait(3000);
        } while (true);
        m_Adapters.RemoveAllAdapters();
        CoUninitialize();
    }
    void Dump()
    {
        CMutexProtect pr(m_AdaptersMutex);
        for (UINT i = 0; i < m_Adapters.GetCount(); ++i)
        {
            m_Adapters[i].Dump();
        }
        Log("Done (%d adapters)", m_Adapters.GetCount());
    }
private:
    CEvent m_ThreadEvent;
    CVirtioAdaptersArray m_Adapters;
    CMutex m_AdaptersMutex;
private:
    NETKVMD_ADAPTER m_IoctlBuffer[256];
    void SyncVirtioAdapters()
    {
        CMutexProtect pr(m_AdaptersMutex);
        NETKVMD_ADAPTER* adapters = m_IoctlBuffer;
        ULONG n = 0;
        // device open-close scope
        {
            CNetkvmDeviceFile d;
            if (!d.Usable()) {
                m_Adapters.RemoveAllAdapters();
            }
            if (!d.ControlGet(IOCTL_NETKVMD_QUERY_ADAPTERS, m_IoctlBuffer, sizeof(m_IoctlBuffer)))
            {
                m_Adapters.RemoveAllAdapters();
            }
            else
            {
                n = d.Returned() / sizeof(NETKVMD_ADAPTER);
            }
        }
        // update existing adapters, add new ones
        for (ULONG i = 0; i < n; ++i)
        {
            bool found = false;
            for (ULONG j = 0; !found && j < m_Adapters.GetCount(); ++j)
            {
                found = m_Adapters[j].Match(adapters[i].MacAddress);
                if (!found) continue;
                m_Adapters[j].Update(adapters[i]);
            }
            if (found) continue;
            CVirtioAdapter a(adapters[i].MacAddress);
            a.Update(adapters[i]);
            m_Adapters.Add(a);
        }
        // remove all non-present adapters
        for (ULONG i = 0; i < m_Adapters.GetCount(); ++i)
        {
            bool found = false;
            for (ULONG j = 0; !found && j < n; ++j)
            {
                found = m_Adapters[i].Match(adapters[j].MacAddress);
            }
            if (found) continue;
            m_Adapters.RemoveAdapter(i);
            i--;
        }
    }
};

static CProtocolServiceImplementation DummyService;

static void UninstallProtocol()
{
    puts("Uninstalling VIOPROT");
    system("netcfg -v -u VIOPROT");
    CInfDirectory dir;
    puts("Scan for protocol INF file...");
    CFileFinder f(dir + L"oem*.inf");
    f.Process([&](const TCHAR* Name)
        {
            CString completeName = dir + Name;
            //printf("Checking %S...\n", Name);
            CString s;
            s.Format(L"type %s | findstr /i vioprot.cat", completeName.GetString());
            int res = _wsystem(s);
            if (!res)
            {
                printf("Uninstalling %S... ", Name);
                res = SetupUninstallOEMInf(Name, SUOI_FORCEDELETE, NULL);
                if (res)
                {
                    puts("Done");
                }
                else
                {
                    printf("Not done, error, error %d", GetLastError());
                }
            }
            return true;
        });
}

static bool InstallProtocol()
{
    puts("Installing VIOPROT");
    return !system("netcfg -v -l vioprot.inf -c p -i VIOPROT");
}

int __cdecl main(int argc, char **argv)
{
    CStringA s;
    if (argc > 1)
    {
       s = argv[1];
       if (!s.CompareNoCase("x"))
       {
           ProcessProtocolUninstall();
           return 0;
       }
    }
    if (CServiceImplementation::CheckInMain())
    {
        return 0;
    }
    if (!s.IsEmpty())
    {
        if (!s.CompareNoCase("i"))
        {
            if (DummyService.Installed())
            {
                puts("Already installed");
            }
            else if (InstallProtocol())
            {
                puts("Protocol installed");
            }
        }
        if (!s.CompareNoCase("u"))
        {
            if (DummyService.Installed())
            {
                UninstallProtocol();
                DummyService.Uninstall();
            }
            else
            {
                puts("Service is not installed");
                UninstallProtocol();
            }
        }
        if (!s.CompareNoCase("q"))
        {
            puts(DummyService.Installed() ? "installed" : "not installed");
        }
        if (!s.CompareNoCase("d"))
        {
            DummyService.Control(CProtocolServiceImplementation::ctlDump);
        }
    }
    else
    {
        puts("i(nstall)|u(ninstall)|q(uery)");
    }
    return 0;
}
