#pragma once

enum Status { None, Active, Reset };


class GpuAdapter
{
public:
    GpuAdapter(const std::wstring LinkName);
    virtual ~GpuAdapter() { Close(); }
    Status GetStatus(void) { return m_Flag; }
    void SetStatus(Status flag) { m_Flag = flag; }
private:
#pragma warning( push )
#pragma warning(disable: 26495)
    GpuAdapter() { ; }
#pragma warning( pop )
    static DWORD WINAPI ServiceThread(GpuAdapter*);
    void Run();
    void Init();
    void Close();
    bool QueryAdapterId();
    UINT GetNumbersOfPathArrayElements(void) { return m_PathArrayElements; }
    UINT GetNumbersOfModeInfoArrayElements(void) { return m_ModeInfoArrayElements; }
    bool GetCurrentResolution(PVIOGPU_DISP_MODE mode);
    DISPLAYCONFIG_MODE_INFO* GetDisplayConfig(UINT index);
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
    DISPLAYCONFIG_PATH_INFO* m_pDisplayPathInfo;
    DISPLAYCONFIG_MODE_INFO* m_pDisplayModeInfo;
    Status m_Flag;
};

