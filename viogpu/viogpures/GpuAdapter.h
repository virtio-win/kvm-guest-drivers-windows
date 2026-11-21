/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

constexpr UINT INVALID_PATH_IDX = UINT_MAX;

enum Status
{
    None,
    Active,
    Reset
};

class GpuAdapter
{
  public:
    GpuAdapter(const std::wstring LinkName);
    virtual ~GpuAdapter()
    {
        Close();
    }
    Status GetStatus(void)
    {
        return m_Flag;
    }
    void SetStatus(Status flag)
    {
        m_Flag = flag;
    }
    void Init();
    bool SetCustomResolution(USHORT Width, USHORT Height);

  private:
    void Close();
    bool QueryAdapterId();
    UINT GetNumbersOfPathArrayElements(void)
    {
        return m_PathArrayElements;
    }

    bool GetCustomResolution(PVIOGPU_DISP_MODE mode);
    bool SetResolution(PVIOGPU_DISP_MODE mode);
    LONG UpdateDisplayConfig(void);
    void ClearDisplayConfig(void);

  public:
    std::wstring m_DeviceName;

  private:
    HDC m_hDC;
    D3DKMT_HANDLE m_hAdapter;
    // The driver's adapter id (VIOGPU_GET_DEVICE_ID): the driver answers the escapes, so the display is its.
    ULONG m_Id;
    // This display's path in the last QueryDisplayConfig snapshot of the active paths, or INVALID_PATH_IDX when
    // the display has no active path, as when the user switched it off, or the snapshot failed.
    UINT m_PathIdx;
    UINT m_PathArrayElements;
    UINT m_ModeInfoArrayElements;
    DISPLAYCONFIG_PATH_INFO *m_pDisplayPathInfo;
    DISPLAYCONFIG_MODE_INFO *m_pDisplayModeInfo;
    Status m_Flag;
};
