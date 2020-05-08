/*
 * Copyright (c) 2020 Oracle Corporation
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

#pragma once

#include "netkvmnoif.h"

class CNotifyObject :
    public CComObjectRoot,
    public CComCoClass<CNotifyObject, &CLSID_CNotifyObject>,
    public INetCfgComponentControl,
    public INetCfgComponentSetup,
    public INetCfgComponentNotifyBinding,
    public INetCfgComponentNotifyGlobal
{
private:
    INT CheckProtocolandDevInf(INetCfgBindingPath  *pNetCfgBindingPath,
        INetCfgComponent   **ppUpNetCfgCom,
        INetCfgComponent   **ppLowNetCfgCom);
    HRESULT EnableVFBindings(INetCfgComponent *pNetCfgCom, BOOL bEnable);
    VOID EnableBinding(INetCfgBindingPath *pNetCfgBindPath, BOOL bEnable);
public:
    BEGIN_COM_MAP(CNotifyObject)
        COM_INTERFACE_ENTRY(INetCfgComponentControl)
        COM_INTERFACE_ENTRY(INetCfgComponentSetup)
        COM_INTERFACE_ENTRY(INetCfgComponentNotifyBinding)
        COM_INTERFACE_ENTRY(INetCfgComponentNotifyGlobal)
    END_COM_MAP()

    DECLARE_REGISTRY_RESOURCEID(IDR_REG_NETKVM_NOTIFY_OBJECT)

    CNotifyObject() {}
    ~CNotifyObject() {}
protected:
    //INetCfgNotifyBinding
    STDMETHOD(QueryBindingPath)(DWORD, INetCfgBindingPath *) { return S_OK; }
    STDMETHOD(NotifyBindingPath)(IN DWORD dwChangeFlag,
        IN INetCfgBindingPath *pNetCfgBindPath);

    //INetCfgNotifyGlobal
    STDMETHOD(GetSupportedNotifications)(OUT DWORD *Flags)
    {
        *Flags = NCN_ADD | NCN_REMOVE | NCN_ENABLE | NCN_DISABLE | NCN_BINDING_PATH | NCN_NET | NCN_NETTRANS;
        return S_OK;
    }
    STDMETHOD(SysQueryBindingPath)(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBindPath);
    STDMETHOD(SysNotifyBindingPath)(DWORD, INetCfgBindingPath *) { return S_OK; }
    STDMETHOD(SysNotifyComponent)(DWORD, INetCfgComponent *) { return S_OK; }

    //INetCfgComponentControl (for future extension)
    STDMETHOD(Initialize)(INetCfgComponent *, INetCfg *, BOOL) { return S_OK; }
    STDMETHOD(CancelChanges)() { return S_OK; }
    STDMETHOD(ApplyRegistryChanges)() { return S_OK; }
    STDMETHOD(ApplyPnpChanges)(INetCfgPnpReconfigCallback*) { return S_OK; }

    //INetCfgComponentSetup (for future extension)
    STDMETHOD(Install)(DWORD) { return S_OK; }
    STDMETHOD(Upgrade)(DWORD, DWORD) { return S_OK; }
    STDMETHOD(ReadAnswerFile)(PCWSTR, PCWSTR) { return S_OK; }
    STDMETHOD(Removing)() { return S_OK; }
};
