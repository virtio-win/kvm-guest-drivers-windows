@echo off
setlocal enabledelayedexpansion

set proj_path=%~dp0
set "_t_EWDK_Mnts=Mounted Image"
rem set "_t_EWDK_Mnts=Folder on Disk"
set VIRTIO_WIN_NO_ARM=1
rem set _BUILD_DISABLE_SDV=Yes
set SKIP_SDV_ACTUAL=1
rem set CODEQL_OFFLINE_ONLY=Yes
rem set CODEQL_RUN_BLIND=Yes
set _proj_dir=.
set drv_idx=1
for /d %%i in (*) do @(
  set _drv_dir=%%i
  if not "!_drv_dir:~0,1!"=="." (
    if not "!_drv_dir!"=="build" (
      if not "!_drv_dir!"=="Documentation" (
        if not "!_drv_dir!"=="Tools" (
          if "!_drv_dirs!"=="" (
            set _drv_dirs=!_drv_dir!
            set drv_cnt=1
          ) else (
            set _drv_dirs=!_drv_dirs! !_drv_dir!
            set /a drv_cnt=!drv_cnt!+1
          )
        )
      )
    )
  )
)

rem Colour mods should work from WIN10_TH2
rem Get the ANSI ESC character [0x27]
for /f "tokens=2 usebackq delims=#" %%i in (`"prompt #$H#$E# & echo on & for %%i in (1) do rem"`) do @set z_esc=%%i
rem Prepare pallette
set "_c_Gry="40;90m""
set "_c_Red="40;91m""
set "_c_Grn="40;92m""
set "_c_Yel="40;93m""
set "_c_Cyn="40;96m""
set "_c_Wht="40;37m""
set "_s_Hdr="1;40;96m""

:_proto_menu
if "%_t_EWDK_Mnts%"=="Mounted Image" (
  set EWDK11_DIR=E:
  set EWDK11_24H2_DIR=F:
) else (
  set EWDK11_DIR=C:\EWDK11
  set EWDK11_24H2_DIR=C:\EWDK11_24H2
)
if "%SKIP_SDV_ACTUAL%"=="1" (set _t_SkipSDV=Yes) else (set _t_SkipSDV=No)
if "%VIRTIO_WIN_NO_ARM%"=="1" (set _t_NoARM=No) else (set _t_NoARM=Yes)
if "%VIRTIO_WIN_SDV_2022%"=="1" (set _t_SDV_SysDrivers=Yes) else (set _t_SDV_SysDrivers=No)
if "%_BUILD_DISABLE_SDV%"=="" (set _t_NoSDV=No) else (set _t_NoSDV=Yes)
if "%CODEQL_OFFLINE_ONLY%"=="" (set _t_CodeQL_Mode=Online) else (set _t_CodeQL_Mode=Offline) 
if not "%CODEQL_RUN_BLIND%"=="" (set _t_CodeQL_Mode=Blind) 
if "%_t_Win10%"=="Yes" (set _t_Win10=Yes) else (set _t_Win10=No)
if "%_t_Win11%"=="No" (set _t_Win11=No) else (set _t_Win11=Yes)
if "%_t_Win10_SDV%"=="Yes" (set _t_Win10_SDV=Yes) else (set _t_Win10_SDV=No)
if "%_t_Win11_SDV%"=="Yes" (set _t_Win11_SDV=Yes) else (set _t_Win11_SDV=No)
if "%_t_ARM64%"=="Yes" (set _t_ARM64=Yes) else (set _t_ARM64=No)
if "%_t_x86%"=="Yes" (set _t_x86=Yes) else (set _t_x86=No)
if "%_t_AMD64%"=="No" (set _t_AMD64=No) else (set _t_AMD64=Yes)
if "%_t_Env%"=="" set _t_Env=A
if "%_proj_dir%"=="." (
  set "_proj_choice=ALL drivers in the repository"
  set "proj_path=%~dp0"
) else (
  set "_proj_choice=%_proj_dir%"
  set "proj_path=%~dp0%_proj_dir%"
)
call :_get_params
cls
title Rapid Prototyping Build Menu
mode con cols=78 lines=62
color 07
echo.
call :_color_echo %_s_Hdr% "         Rapid Prototyping Build Menu for Windows KVM Guest Drivers"
echo.
echo         [T] Toggle Enterprise WDK location
if "%_t_EWDK_Mnts%"=="Mounted Image" (
  echo             EWDK11_DIR [21H2] = %EWDK11_DIR%                    [%_t_EWDK_Mnts%]
  echo             EWDK11_24H2_DIR   = %EWDK11_24H2_DIR%                    [%_t_EWDK_Mnts%]
) else (
  echo             EWDK11_DIR [21H2] = %EWDK11_DIR%            [%_t_EWDK_Mnts%]
  echo             EWDK11_24H2_DIR   = %EWDK11_24H2_DIR%       [%_t_EWDK_Mnts%]
)
echo.
if "%_t_NoARM%"=="Yes" (
  call :_color_echo %_c_Wht% "        [1] Build for ARM (unset VIRTIO_WIN_NO_ARM variable)    [%_t_NoARM%]"
) else (
  call :_color_echo %_c_Wht% "        [1] Build for ARM (unset VIRTIO_WIN_NO_ARM variable)     " %_c_Yel% "[%_t_NoARM%]"
)
echo.
if "%_t_NoSDV%"=="No" (
  call :_color_echo %_c_Wht% "        [2] Disable Code Analysis [" %_c_Cyn% "*" %_c_Wht% "] (Var: _BUILD_DISABLE_SDV)  [%_t_NoSDV%]"
) else (
  call :_color_echo %_c_Wht% "        [2] Disable Code Analysis [" %_c_Cyn% "*" %_c_Wht% "] (Var: _BUILD_DISABLE_SDV) " %_c_Yel% "[%_t_NoSDV%]"
)
echo.
if "%_t_SDV_SysDrivers%"=="Yes" (
  call :_color_echo %_c_Wht% "        [3] Perform SDV Build of System Drivers                 " %_c_Yel% "[%_t_SDV_SysDrivers%]"
) else (
  call :_color_echo %_c_Wht% "        [3] Perform SDV Build of System Drivers                  [%_t_SDV_SysDrivers%]"
)
call :_color_echo %_c_Wht% "            (Variable:  VIRTIO_WIN_SDV_2022)"
echo.
if "%_t_SkipSDV%"=="Yes" (
  call :_color_echo %_c_Wht% "        [4] Skip SDV in Code Analysis (Var: SKIP_SDV_ACTUAL)    " %_c_Red% "[%_t_SkipSDV%]"
) else (
  call :_color_echo %_c_Wht% "        [4] Skip SDV in Code Analysis (Var: SKIP_SDV_ACTUAL)     [%_t_SkipSDV%]"
)
echo.
if "%_t_CodeQL_Mode%"=="Online" call :_color_echo %_c_Wht% "        [M] Set CodeQL Download Mode                         [%_t_CodeQL_Mode%]"
if "%_t_CodeQL_Mode%"=="Offline" call :_color_echo %_c_Wht% "        [M] Set CodeQL Download Mode                        " %_c_Yel% "[%_t_CodeQL_Mode%]"
if "%_t_CodeQL_Mode%"=="Blind" call :_color_echo %_c_Wht% "        [M] Set CodeQL Download Mode                          " %_c_Red% "[%_t_CodeQL_Mode%]"
call :_color_echo %_c_Wht% "            (Vars: CODEQL_OFFLINE_ONLY + CODEQL_RUN_BLIND)"
echo.
echo         Environments :
echo.
call :_color_echo %_c_Grn% "        [A]" %_c_Wht% " Dynamic environment via build script - RECOMMENDED"
echo.
echo         [E] Use Windows 11, 21H2 [22000] EWDK to build all targets
echo.
echo         [F] Use Windows 11, 24H2 [26100] EWDK to build all targets
echo             Note : Win10_SDV targets use the 21H2 [22000] EWDK to
echo                    perform SDV so as to ensure the DVL is valid.
echo.
echo         Do a Build :
echo.
call :_color_echo %_c_Red% "           [B]" %_c_Wht% " Execute a build using the options specified below."
echo.
call :_color_echo %_c_Yel% "               [D]" %_c_Wht% " Build for: " %_c_Gry% "%_proj_choice%"
echo.
echo         To build other individual drivers choose an environment above
echo         and issue build commands from the desired project directory.
echo.
echo                [5] Target Windows 10 (No-Analysis Build) [%_t_Win10%]
echo                [6] Target Windows 11 (No-Analysis Build) [%_t_Win11%]
if "%_t_ARM64%"=="No"  call :_color_echo %_c_Wht% "               [7] Build for arm64 platforms             " %_c_Yel% "[%_t_ARM64%]"
if "%_t_ARM64%"=="Yes" call :_color_echo %_c_Wht% "               [7] Build for arm64 platforms             [%_t_ARM64%]"
echo                [8] Build for x86 platforms               [%_t_x86%]
echo                [9] Build for amd64 platforms             [%_t_AMD64%]
call :_color_echo %_c_Wht% "               [Y] Windows 10 Code Analysis Build [" %_c_Cyn% "*" %_c_Wht% "]    [%_t_Win10_SDV%]"
call :_color_echo %_c_Wht% "               [Z] Windows 11 Code Analysis Build [" %_c_Cyn% "*" %_c_Wht% "]    [%_t_Win11_SDV%]"
if "%_t_Env%"=="A" (
  call :_color_echo %_c_Wht% "               [U] Toggle Build Environment              " %_c_Grn% "[%_t_Env%]"
) else (
  call :_color_echo %_c_Wht% "               [U] Toggle Build Environment              " %_c_Yel% "[%_t_Env%]"
)
call :_color_echo %_c_Wht% "                   Choose A, E or F. " %_c_Grn% "A" %_c_Wht% " is RECOMMENDED."
echo.
call :_color_echo %_c_Wht% "                   buildAll.bat parameters: " %_c_Gry% "[%__cmd_line_args__%]"
echo.
call :_color_echo %_c_Cyn% "        [S]" %_c_Wht% " Sign built drivers with VirtIO Test Cert"
echo.
call :_color_echo %_c_Yel% "        [C]" %_c_Wht% " Clean repository" %_c_Yel% "       [Q]/[V]" %_c_Wht% " Clean: Quiet OR Verbose"
echo.
call :_color_echo %_c_Wht% "        [" %_c_Cyn% "*" %_c_Wht% "] Includes SDV, CodeQL, CA and DVL operations. A pre-"
echo             built or simultaneous No-Analysis Build is required.
echo.
call :_color_echo %_c_Red% "        CAUTION " %_c_Wht% ": Please be aware that Static Driver Verifier (SDV)"
echo                   builds can take a long time to complete.
echo.
choice /c 123456789ABCDEFMQSTUVXYZ /n /m ".              Choose a menu option, or press [X] to Exit: "
set _ch_lvl=%ERRORLEVEL%
if %_ch_lvl%==24 (
  if "%_t_Win11_SDV%"=="Yes" (
    set _t_Win11_SDV=No
    if "%_t_Win10_SDV%"=="No" (
      set _t_Win10=No
      set _t_Win11=Yes
    ) else (
      set _t_Win10=No
      set _t_Win11=No
    )
  ) else (
    if "%_BUILD_DISABLE_SDV%"=="" (
      set _t_Win11_SDV=Yes
      set _t_Win11=No
    ) else (
      set _t_Win11_SDV=No
      set _t_Win11=Yes
    )
    set _t_Win10_SDV=No
    set _t_Win10=No
  )
)&goto :_proto_menu
if %_ch_lvl%==23 (
  if "%_t_Win10_SDV%"=="Yes" (
    set _t_Win10_SDV=No
    if "%_t_Win11_SDV%"=="No" (
      set _t_Win10=No
      set _t_Win11=Yes
    ) else (
      set _t_Win10=No
      set _t_Win11=No
    )
  ) else (
    if "%_BUILD_DISABLE_SDV%"=="" (
      set _t_Win10_SDV=Yes
      set _t_Win11=No
    ) else (
      set _t_Win10_SDV=No
      set _t_Win11=Yes
    )
    set _t_Win11_SDV=No
    set _t_Win10=No
  )
)&goto :_proto_menu
if %_ch_lvl%==22 goto :_quitter
if %_ch_lvl%==21 (start /w /max %comspec% /c "pushd %proj_path% && clean.bat -debug & pause")&goto :_proto_menu
if %_ch_lvl%==20 (if "%_t_Env%"=="A" (set _t_Env=E) else (if "%_t_Env%"=="E" (set _t_Env=F) else (if "%_t_Env%"=="F" (set _t_Env=A))))&goto :_proto_menu
if %_ch_lvl%==19 (
  if "%_t_EWDK_Mnts%"=="Mounted Image" (
    set "_t_EWDK_Mnts=Folder on Disk"
  ) else (
    set "_t_EWDK_Mnts=Mounted Image"
  )
)&goto :_proto_menu
if %_ch_lvl%==18 (
  if "%_proj_dir%"=="." (
    start /w /max %comspec% /c "pushd %proj_path% && call .\build\signAll.bat & pause"
  ) else (
    start /w /max %comspec% /c "pushd %proj_path% && call ..\build\signAll.bat & pause"
  )
)&goto :_proto_menu
if %_ch_lvl%==17 (start /w /max %comspec% /c "pushd %proj_path% && clean.bat -quiet & pause")&goto :_proto_menu
if %_ch_lvl%==16 (
  if "%_t_CodeQL_Mode%"=="Online" (
    set _t_CodeQL_Mode=Offline
    set CODEQL_OFFLINE_ONLY=Yes
    set CODEQL_RUN_BLIND=
  ) else (
    if "%_t_CodeQL_Mode%"=="Offline" (
      set _t_CodeQL_Mode=Blind
      set CODEQL_OFFLINE_ONLY=
      set CODEQL_RUN_BLIND=Yes
    ) else (
      if "%_t_CodeQL_Mode%"=="Blind" (
        set _t_CodeQL_Mode=Online
        set CODEQL_OFFLINE_ONLY=
        set CODEQL_RUN_BLIND=
      )
    )
  )
)&goto :_proto_menu
if %_ch_lvl%==15 (start %comspec% /c "pushd %proj_path% && %EWDK11_24H2_DIR%\LaunchBuildEnv.cmd")&goto :_proto_menu
if %_ch_lvl%==14 (start %comspec% /c "pushd %proj_path% && %EWDK11_DIR%\LaunchBuildEnv.cmd")&goto :_proto_menu
if %_ch_lvl%==13 (
  if %drv_idx% GTR %drv_cnt% (
    set drv_idx=0
  )
  if !drv_idx! EQU 0 (
    set _proj_dir=.
  ) else (
    for /f "tokens=%drv_idx%" %%i in ("%_drv_dirs%") do @set _proj_choice=%%i
    set _proj_dir=!_proj_choice!
  )
)&& set /a drv_idx=!drv_idx!+1 &goto :_proto_menu
if %_ch_lvl%==12 (start /w /max %comspec% /c "pushd %proj_path% && clean.bat & pause")&goto :_proto_menu
if %_ch_lvl%==11 (call :_one_shot )&goto :_proto_menu
if %_ch_lvl%==10 (start %comspec% /k "pushd %proj_path%")&goto :_proto_menu
if %_ch_lvl%==9 (
  if "%_t_AMD64%"=="Yes" (
    set _t_AMD64=No
    if "%_t_x86%"=="No" (
      set _t_x86=Yes
    )
  ) else (
    set _t_AMD64=Yes
  )
)&goto :_proto_menu
if %_ch_lvl%==8 (
  if "%_t_x86%"=="Yes" (
    set _t_x86=No
    if "%_t_AMD64%"=="No" (
      set _t_AMD64=Yes
    )
  ) else (
    set _t_x86=Yes
  )
)&goto :_proto_menu
if %_ch_lvl%==7 (
  if "%_t_ARM64%"=="Yes" (
    set _t_ARM64=No
    set VIRTIO_WIN_NO_ARM=1
  ) else (
    set _t_ARM64=Yes
    set VIRTIO_WIN_NO_ARM=
  )
)&goto :_proto_menu
if %_ch_lvl%==6 (
  if "%_t_Win11%"=="Yes" (
    set _t_Win11=No
    if "%_t_Win10%"=="No" (
      set _t_Win10=Yes
    )
  ) else (
    set _t_Win11=Yes
  )
)&goto :_proto_menu
if %_ch_lvl%==5 (
  if "%_t_Win10%"=="Yes" (
    set _t_Win10=No
    if "%_t_Win11%"=="No" (
    set _t_Win11=Yes
    )
  ) else (
    set _t_Win10=Yes
  )
)&goto :_proto_menu
if %_ch_lvl%==4 (if "%SKIP_SDV_ACTUAL%"=="1" (set SKIP_SDV_ACTUAL=) else (set SKIP_SDV_ACTUAL=1))&goto :_proto_menu
if %_ch_lvl%==3 (if "%VIRTIO_WIN_SDV_2022%"=="1" (set VIRTIO_WIN_SDV_2022=) else (set VIRTIO_WIN_SDV_2022=1))&goto :_proto_menu
if %_ch_lvl%==2 (
  if "%_BUILD_DISABLE_SDV%"=="" (
    set _BUILD_DISABLE_SDV=Yes
    set _t_Win10_SDV=No
    set _t_Win11_SDV=No
    if "%_t_Win10%"=="No" (
      set _t_Win11=Yes
    ) else (
      set _t_Win10=Yes
      set _t_Win11=No
    )
  ) else (
    set _BUILD_DISABLE_SDV=
  )
)&goto :_proto_menu
if %_ch_lvl%==1 (
  if "%_t_ARM64%"=="No" (
    set VIRTIO_WIN_NO_ARM=
    set _t_ARM64=Yes
  ) else (
    set VIRTIO_WIN_NO_ARM=1
    set _t_ARM64=No
  )
)&goto :_proto_menu
goto :_proto_menu

:_color_echo
echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof

:_tgt_mix
if "%_t_Win11%"=="Yes" (
  if "%_t_Win10%"=="Yes" (
    set tgt_args=
    if "%_BUILD_DISABLE_SDV%"=="" (
      set _t_Win10_SDV=Yes
      set _t_Win11_SDV=Yes
    ) else (
      set _t_Win10_SDV=No
      set _t_Win11_SDV=No
    )
  ) else (
    set tgt_args=Win11
    set _t_Win10_SDV=No
    set _t_Win11_SDV=No
  )
) else (
  if "%_t_Win10%"=="Yes" (
    set tgt_args=Win10
    set _t_Win10_SDV=No
    set _t_Win11_SDV=No
  ) else (
    if "%_BUILD_DISABLE_SDV%"=="" (
      if "%_t_Win10_SDV%"=="Yes" (
        set tgt_args=Win10_SDV
      ) else (
        set tgt_args=Win11_SDV
      )
    ) else (
      set tgt_args=Win11
      set _t_Win10=No
      set _t_Win11=Yes
      set _t_Win10_SDV=No
      set _t_Win11_SDV=No
    )
  )
)
if "%_t_Win10%"=="Yes" (
  if "%_t_Win11%"=="Yes" (
    set tgt_args=
    if "%_BUILD_DISABLE_SDV%"=="" (
      set _t_Win10_SDV=Yes
      set _t_Win11_SDV=Yes
    ) else (
      set _t_Win10_SDV=No
      set _t_Win11_SDV=No
    )
  ) else (
    set tgt_args=Win10
    set _t_Win10_SDV=No
    set _t_Win11_SDV=No
  )
) else (
  if "%_t_Win11%"=="Yes" (
    set tgt_args=Win11
    set _t_Win10_SDV=No
    set _t_Win11_SDV=No
  ) else (
    if "%_BUILD_DISABLE_SDV%"=="" (
      if "%_t_Win10_SDV%"=="Yes" (
        set tgt_args=Win10_SDV
      ) else (
        set tgt_args=Win11_SDV
      )
    ) else (
      set tgt_args=Win10
      set _t_Win10=Yes
      set _t_Win11=No
      set _t_Win10_SDV=No
      set _t_Win11_SDV=No
    )
  )
)
goto :eof

:_arch_mix
if "%_t_AMD64%"=="Yes" (
  if "%_t_x86%"=="Yes" (
    set arch_args=
  ) else (
    set arch_args=amd64
  )
) else (
  if "%_t_x86%"=="Yes" (
    set arch_args=x86
  )
)
if "%_t_x86%"=="Yes" (
  if "%_t_AMD64%"=="Yes" (
    set arch_args=
  ) else (
    set arch_args=x86
  )
) else (
  if "%_t_AMD64%"=="Yes" (
    set arch_args=amd64
  )
)
goto :eof

:_get_params
set tgt_args=
set arch_args=
call :_tgt_mix
call :_arch_mix
if not "%tgt_args%"=="" (set __cmd_line_args__=%tgt_args%) else (set __cmd_line_args__=)
if not "%arch_args%"=="" (
  if "%tgt_args%"=="" (
    set __cmd_line_args__=%arch_args%
  ) else (
    set __cmd_line_args__=%__cmd_line_args__% %arch_args%
  )
)
goto :eof

:_one_shot
setlocal
if "%_t_Env%"=="A" (call start /w /max %comspec% /c "pushd %proj_path% && buildAll.bat %__cmd_line_args__% & pause" )
if "%_t_Env%"=="E" (call start /w /max %comspec% /c "pushd %proj_path% && %EWDK11_DIR%\BuildEnv\SetupBuildEnv.cmd && buildAll.bat %__cmd_line_args__% & pause")
if "%_t_Env%"=="F" (call start /w /max %comspec% /c "pushd %proj_path% && %EWDK11_24H2_DIR%\BuildEnv\SetupBuildEnv.cmd && buildAll.bat %__cmd_line_args__% & pause")
endlocal
goto :eof

:_quitter
cls
endlocal
