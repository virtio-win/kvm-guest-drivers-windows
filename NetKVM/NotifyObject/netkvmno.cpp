/*
 * Copyright (c) 2008-2017 Red Hat, Inc.
 * Copyright (c) 2020 Oracle and/or its affiliates
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

#define  VFDEV          0x0001
#define  VIOPRO         0x0010

#define SET_FLAGS(Flag, Val)      (Flag) = ((Flag) | (Val))
#define TEST_FLAGS(Flag, Val)     ((Flag) & (Val))

/*
 * Check whether the binding is the one we need - the upper is the VirtIO
 * protocol and the lower is the VF miniport driver.
 * Return value - iRet, its bitmap(VFDEV/VIOPRO) represents existence
 * of the VirtIO protocol and VF miniport in the binding.
 */
INT CNotifyObject::CheckProtocolandDevInf(INetCfgBindingPath *pNetCfgBindingPath,
                                          INetCfgComponent  **ppUpNetCfgCom,
                                          INetCfgComponent  **ppLowNetCfgCom
)
{
    IEnumNetCfgBindingInterface    *pEnumNetCfgBindIf = NULL;
    INetCfgBindingInterface        *pNetCfgBindIf = NULL;
    ULONG                           ulNum;
    LPWSTR                          pszwLowInfId = NULL;
    LPWSTR                          pszwUpInfId = NULL;
    INT                             ret = 0;

    Trace("%s", __FUNCTION__);

    if (S_OK == pNetCfgBindingPath->EnumBindingInterfaces(&pEnumNetCfgBindIf) &&
        S_OK == pEnumNetCfgBindIf->Next(1, &pNetCfgBindIf, &ulNum) &&
        S_OK == pNetCfgBindIf->GetUpperComponent(ppUpNetCfgCom) &&
        S_OK == pNetCfgBindIf->GetLowerComponent(ppLowNetCfgCom) &&
        S_OK == (*ppUpNetCfgCom)->GetId(&pszwUpInfId) &&
        S_OK == (*ppLowNetCfgCom)->GetId(&pszwLowInfId))
    {
        // Upper is VIO protocol
        if (!_wcsicmp(pszwUpInfId, NETKVM_PROTOCOL_NAME_W))
            SET_FLAGS(ret, VIOPRO);
        // Lower is VF device miniport
        if (IsSupportedSRIOVAdapter(pszwLowInfId))
            SET_FLAGS(ret, VFDEV);
        Trace("pszwUpInfId %S, pszwLowInfId %S", pszwUpInfId, pszwLowInfId);
    }

    if (pszwLowInfId)
        CoTaskMemFree(pszwLowInfId);
    if (pszwUpInfId)
        CoTaskMemFree(pszwUpInfId);
    ReleaseObj(pNetCfgBindIf);
    ReleaseObj(pEnumNetCfgBindIf);

    Trace("%s <=iRet 0x%X", __FUNCTION__, ret);
    return ret;
}

/*
 * When the new binding(VirtIO Protocol<->VF Miniport) is added, enumerate
 * all other bindings(non VirtIO Protocol<->VF Miniport) and disable them.
 * When the a binding(VirtIO Protocol<->VF Miniport) is removed, enumerate
 * all other bindings(non VirtIO Protocol<->VF Miniport) and enable them.
 */
STDMETHODIMP CNotifyObject::NotifyBindingPath(IN DWORD dwChangeFlag, IN INetCfgBindingPath *pNetCfgBindPath)
{
    INetCfgComponent     *pUpNetCfgCom = NULL;
    INetCfgComponent     *pLowNetCfgCom = NULL;
    BOOL                  bAdd, bRemove;
    INT                   iRet = 0;

    Trace("-->%s change flags 0x%X", __FUNCTION__, dwChangeFlag);

    bAdd = dwChangeFlag & NCN_ADD;
    bRemove = dwChangeFlag & NCN_REMOVE;

    // Check and operate when binding is being added or removed
    if (bAdd || bRemove) {
        iRet = CheckProtocolandDevInf(pNetCfgBindPath,
                                      &pUpNetCfgCom, &pLowNetCfgCom);
        if (TEST_FLAGS(iRet, VFDEV) && TEST_FLAGS(iRet, VIOPRO)) {
            if (bAdd) {
                // Enumerate and disable other bindings except for
                // VIOprotocol<->VF miniport
                if (EnableVFBindings(pLowNetCfgCom, FALSE))
                    Trace("Failed to disable non VIO protocol to VF miniport");
            }
            else {
                // Enumerate and enable other bindings except for
                // VIOprotocol<->VF miniport
                if (EnableVFBindings(pLowNetCfgCom, TRUE))
                    Trace("Failed to enable non VIO protocol to VF miniport");
            }
        }
    }

    ReleaseObj(pUpNetCfgCom);
    ReleaseObj(pLowNetCfgCom);

    Trace("<--%s", __FUNCTION__);

    return S_OK;
}

//----------------------------------------------------------------------------
// INetCfgComponentNotifyGlobal
//----------------------------------------------------------------------------

//When addition of a binding path is about to occur,
//disable it if it is VirtIO protocol<->non VF miniport binding.
STDMETHODIMP CNotifyObject::SysQueryBindingPath(DWORD dwChangeFlag,
                                                INetCfgBindingPath *pNetCfgBindPath)
{
    INetCfgComponent     *pUpNetCfgCom;
    INetCfgComponent     *pLowNetCfgCom;
    INT                  iRet = 0;
    HRESULT              hResult = S_OK;

    pUpNetCfgCom = NULL;
    pLowNetCfgCom = NULL;
    Trace("-->%s", __FUNCTION__);

    if (dwChangeFlag & (NCN_ENABLE | NCN_ADD)) {
        iRet = CheckProtocolandDevInf(pNetCfgBindPath,
                                      &pUpNetCfgCom, &pLowNetCfgCom);
        if (!TEST_FLAGS(iRet, VFDEV) && TEST_FLAGS(iRet, VIOPRO)) {
            // Upper protocol is virtio protocol and lower id is not
            // vf device id, disable the binding.
            hResult = NETCFG_S_DISABLE_QUERY;
        }

        ReleaseObj(pUpNetCfgCom);
        ReleaseObj(pLowNetCfgCom);
    }
    Trace("<--%s HRESULT = %x", __FUNCTION__, hResult);

    return hResult;
}

//Enable/Disable the bindings of non VIO protocols to the VF miniport
HRESULT CNotifyObject::EnableVFBindings(INetCfgComponent *pNetCfgCom,
                                        BOOL bEnable)
{
    IEnumNetCfgBindingPath      *pEnumNetCfgBindPath;
    INetCfgBindingPath          *pNetCfgBindPath;
    INetCfgComponentBindings    *pNetCfgComBind;
    HRESULT                     hResult;
    ULONG                       ulNum;

    Trace("-->%s bEnable = %d", __FUNCTION__, bEnable);

    // Get the binding path enumerator.
    pEnumNetCfgBindPath = NULL;
    pNetCfgComBind = NULL;
    hResult = pNetCfgCom->QueryInterface(IID_INetCfgComponentBindings,
                                         (PVOID *)&pNetCfgComBind);
    if (S_OK == hResult) {
        hResult = pNetCfgComBind->EnumBindingPaths(EBP_ABOVE,
                                                   &pEnumNetCfgBindPath);
        ReleaseObj(pNetCfgComBind);
    }
    else
        return hResult;

    if (hResult == S_OK) {
        pNetCfgBindPath = NULL;
        hResult = pEnumNetCfgBindPath->Next(1, &pNetCfgBindPath, &ulNum);
        // Enumerate every binding path.
        while (hResult == S_OK) {
            EnableBinding(pNetCfgBindPath, bEnable);
            ReleaseObj(pNetCfgBindPath);
            pNetCfgBindPath = NULL;
            hResult = pEnumNetCfgBindPath->Next(1, &pNetCfgBindPath, &ulNum);
        }
        ReleaseObj(pEnumNetCfgBindPath);
    }
    else {
        Trace("Failed to get the binding path enumerator, "
                 "bindings will not be %s.", bEnable ? "enabled" : "disabled");
    }

    Trace("%s\n", __FUNCTION__);

    return hResult;
}

//Enable or disable bindings with non VIO protocol
VOID CNotifyObject::EnableBinding(INetCfgBindingPath *pNetCfgBindPath, BOOL bEnable)
{
    INetCfgComponent     *pUpNetCfgCom = NULL;
    INetCfgComponent     *pLowNetCfgCom = NULL;
    INT                   iRet;

    iRet = CheckProtocolandDevInf(pNetCfgBindPath,
                                  &pUpNetCfgCom, &pLowNetCfgCom);
    if (!TEST_FLAGS(iRet, VIOPRO))
        pNetCfgBindPath->Enable(bEnable);

    ReleaseObj(pUpNetCfgCom);
    ReleaseObj(pLowNetCfgCom);
}

CComModule _Module;

BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_CNotifyObject, CNotifyObject)
END_OBJECT_MAP()

// required DLL exports
EXTERN_C_START

STDAPI DllRegisterServer(void)
{
    HRESULT hr = _Module.RegisterServer(true);
    Trace("%s hr %d", __FUNCTION__, hr);
    return hr;
}

// required DLL export
STDAPI DllUnregisterServer(void)
{
    HRESULT hr = _Module.UnregisterServer();
    Trace("%s hr %d", __FUNCTION__, hr);
    return S_OK;
}

STDAPI DllCanUnloadNow(void)
{
    HRESULT hr = _Module.GetLockCount() ? S_OK : S_FALSE;
    Trace("%s hr %d", __FUNCTION__, hr);
    return hr;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    HRESULT hr = _Module.GetClassObject(rclsid, riid, ppv);
    Trace("%s hr %d", __FUNCTION__, hr);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        Trace("%s: Attach =>", __FUNCTION__);
        _Module.Init(ObjectMap, hInstance);
        DisableThreadLibraryCalls(hInstance);
        break;
    case DLL_PROCESS_DETACH:
        Trace("%s: Detach =>", __FUNCTION__);
        _Module.Term();
    }
    Trace("%s: <=", __FUNCTION__);
    return true;
}

EXTERN_C_END
