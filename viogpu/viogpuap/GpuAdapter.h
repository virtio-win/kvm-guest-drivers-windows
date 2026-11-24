/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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
    // Set up: the display has its sync thread. One whose setup failed is set up again when it is found next.
    bool IsReady(void) const
    {
        return m_hThread != NULL;
    }

  private:
#pragma warning(push)
#pragma warning(disable : 26495)
    GpuAdapter()
    {
        ;
    }
#pragma warning(pop)
    static DWORD WINAPI ServiceThread(GpuAdapter *);
    void Run();
    void Init();
    void Close();
    bool QueryAdapterId();
    UINT GetNumbersOfPathArrayElements(void)
    {
        return m_PathArrayElements;
    }
    UINT GetNumbersOfModeInfoArrayElements(void)
    {
        return m_ModeInfoArrayElements;
    }
    // What a sync came to: Waiting for a path or a size, which the driver signals when they come and Run tries again
    // meanwhile, a failed mode change, which Run tries a few times, or a failure only a change can end.
    enum SyncResult
    {
        Synced,
        Waiting,
        SetFailed,
        Failed
    };
    // Which escape gives this display's size: not known yet, VIOGPU_GET_SOURCE_RESOLUTION, or, from a driver without
    // it, VIOGPU_GET_CUSTOM_RESOLUTION.
    enum EscapeKind
    {
        EscapeUnknown,
        EscapeSource,
        EscapeLegacy
    };
    bool GetCurrentResolution(PVIOGPU_DISP_MODE mode);
    DISPLAYCONFIG_MODE_INFO *GetDisplayConfig(void);
    NTSTATUS GetCustomResolution(PVIOGPU_SOURCE_MODE pAnswer);
    ULONGLONG CurrentLayoutKey(void);
    void SetAppliedGeneration(const VIOGPU_SOURCE_MODE &Answer);
    LONG SetResolution(PVIOGPU_DISP_MODE mode);
    SyncResult SyncResolution(void);
    std::wstring CurrentLayout(void);
    LONG UpdateDisplayConfig(void);
    void ClearDisplayConfig(void);

  public:
    std::wstring m_DeviceName;

  private:
    HANDLE m_hThread;
    HANDLE m_hStopEvent;
    // The driver's event for this display's source, which stays set until Run waits on it; with a driver that has
    // none, or when it could not be opened (m_DeviceEventOnly), the device's event, which the driver pulses.
    HANDLE m_hResolutionEvent;
    bool m_DeviceEventOnly;
    EscapeKind m_Escape;
    // Why the last sync did not sync, and the status that said so, for the log line of a display left until the next
    // change.
    const wchar_t *m_SyncProblem;
    LONG m_SyncStatus;
    HDC m_hDC;
    D3DKMT_HANDLE m_hAdapter;
    // The driver's adapter id (VIOGPU_GET_DEVICE_ID), which names its resolution events.
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
