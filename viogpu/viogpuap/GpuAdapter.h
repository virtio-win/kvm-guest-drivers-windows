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
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

enum Status { None, Active, Reset };

class GpuAdapter {
public:
  GpuAdapter(const std::wstring LinkName);
  virtual ~GpuAdapter() { Close(); }
  Status GetStatus(void) { return m_Flag; }
  void SetStatus(Status flag) { m_Flag = flag; }

private:
#pragma warning(push)
#pragma warning(disable : 26495)
  GpuAdapter() { ; }
#pragma warning(pop)
  static DWORD WINAPI ServiceThread(GpuAdapter *);
  void Run();
  void Init();
  void Close();
  bool QueryAdapterId();
  UINT GetNumbersOfPathArrayElements(void) { return m_PathArrayElements; }
  UINT GetNumbersOfModeInfoArrayElements(void) {
    return m_ModeInfoArrayElements;
  }
  bool GetCurrentResolution(PVIOGPU_DISP_MODE mode);
  DISPLAYCONFIG_MODE_INFO *GetDisplayConfig(UINT index);
  bool GetCustomResolution(PVIOGPU_DISP_MODE mode);
  bool SetResolution(PVIOGPU_DISP_MODE mode);
  void UpdateDisplayConfig(void);
  void ClearDisplayConfig(void);

public:
  std::wstring m_DeviceName;

private:
  HANDLE m_hThread;
  HANDLE m_hStopEvent;
  HANDLE m_hResolutionEvent;
  HDC m_hDC;
  D3DKMT_HANDLE m_hAdapter;
  ULONG m_Index;
  UINT m_PathArrayElements;
  UINT m_ModeInfoArrayElements;
  DISPLAYCONFIG_PATH_INFO *m_pDisplayPathInfo;
  DISPLAYCONFIG_MODE_INFO *m_pDisplayModeInfo;
  Status m_Flag;
};
