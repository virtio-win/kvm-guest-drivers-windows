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

class GpuAdapter;

class Notification {
private:
#pragma warning(push)
#pragma warning(disable : 26495)
  Notification() {};
#pragma warning(pop)
public:
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
  Notification(UINT msg, WPARAM wParam, LPARAM lParam)
      : msg(msg), wParam(wParam), lParam(lParam) {};
};

class GpuAdaptersMgr {
public:
  GpuAdaptersMgr() : m_hAdapterNotify(NULL), m_hThread(NULL), m_hWnd(NULL) {}
  ~GpuAdaptersMgr() {}
  BOOL Init();
  void Close();

private:
  static void ProcessPnPNotification(GpuAdaptersMgr *ptr,
                                     Notification newNotification);

protected:
  void FindAdapters();
  BOOL FindDisplayDevice(PDISPLAY_DEVICE lpDisplayDevice, std::wstring &name,
                         PDWORD adapterIndex);
  BOOL GetDisplayDevice(LPCTSTR lpDevice, DWORD iDevNum,
                        PDISPLAY_DEVICE lpDisplayDevice, DWORD dwFlags);
  void AddAdapter(const wchar_t *name);
  void RemoveAllAdapters();
  void InvalidateAdapters();

  static DWORD WINAPI ServiceThread(GpuAdaptersMgr *);
  void Run();
  static LRESULT CALLBACK GlobalWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam);
  HDEVNOTIFY RegisterInterfaceNotify(GUID InterfaceClassGuid);

private:
  std::list<GpuAdapter *> Adapters;
  typedef std::list<GpuAdapter *>::iterator Iterator;

protected:
  HDEVNOTIFY m_hAdapterNotify;
  HANDLE m_hThread;
  HWND m_hWnd;
};
