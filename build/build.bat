@echo off
setlocal enabledelayedexpansion

rem ================================================================================
rem Virtio-win master build script
rem
rem Usage: build.bat <project_or_solution_file_path> <target_os_versions> [<args>]
rem
rem where args can be one or more of
rem   Debug, dbg, chk       .. to build Debug rather than the default release flavor
rem   amd64, x64, 64        .. to build only 64-bit driver
rem   x86, 32               .. to build only 32-bit driver
rem   /Option               .. build command to pass to VS, for example /Rebuild
rem   Win10, Win11          .. target OS version
rem
rem By default the script performs an incremental build of both 32-bit and 64-bit
rem release drivers for all supported target OSes.
rem
rem To do a Static Driver Verifier build, append the _SDV tag to the target OS version,
rem for example Win10_SDV. Where SDV is deprecated, we rely on CodeQL, defaulting to
rem the WHCP_24H2 configuration. To use an earlier configuration specify WHCP_LEGACY.
rem We also continue to run Code Analysis (CA) and Driver Verifier Log (DVL) builds 
rem for use with HCK / WHCP.
rem ================================================================================

rem This is a list of supported build target specifications A_B where A is the
rem VS project configuration name and B is the corresponding platform identifier
rem used in log file names and intermediate directory names. Either of the two can
rem be used in the <target_os_version> command line argument.
set SUPPORTED_BUILD_SPECS=Win10_win10 Win11_win11

set BUILD_TARGETS=%~2
set BUILD_DIR=%~dp1
set BUILD_FILE=%~nx1
set BUILD_NAME=%~n1

rem We do an incremental Release build for all specs and all archs by default
set BUILD_FLAVOR=Release
set BUILD_COMMAND=/Build
set BUILD_SPEC=
set BUILD_ARCH=
set BUILD_FAILED=

rem We have version 24H2 epoch buildtime conditions
rem Paramaters / Arguments:
set WHCP_LEVEL=
rem VSCMD_VER version splits:
set VSCMD_VER_MAJOR=
set VSCMD_VER_MINOR=
set VSCMD_VER_BUILD=
rem VSCMD_VER version specific buildtime vars
set VSCMD_WHCP_LEVEL=
set VIOSOCK_PREBUILD_X86_LIBS=

rem Here we set the Git repo commit hash for our desired CodeQL suites
rem Latest is 99de004870cf44628b6f8c8e28bb2e0c7e3217f0 dated 2024-02-02
set CODEQL_DRIVER_SUITES_REPO_HASH=99de004870cf44628b6f8c8e28bb2e0c7e3217f0
rem We can also use HEAD [44a75acab6decc2676d0e0f045de47a20a238c3c] dated 2024-07-31
rem set CODEQL_DRIVER_SUITES_REPO_HASH=44a75acab6decc2676d0e0f045de47a20a238c3c
rem We will compare against LOCAL_REPO_HASH later
set CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH=
rem If necessary, set CODEQL_OFFLINE_ONLY or CODEQL_RUN_BLIND in the environment before
rem calling this script. Use CODEQL_OFFLINE_ONLY to prevent CodeQL from getting new suites
rem or packages. Use CODEQL_RUN_BLIND to also skip suite and package version checks.
rem
rem WARNING : Setting CODEQL_OFFLINE_ONLY together with WHCP_LEGACY will result in loss
rem           of WHCP_24H2 CodeQL packages. Use with caution.
rem
rem set CODEQL_OFFLINE_ONLY=
rem set CODEQL_RUN_BLIND=
set CODEQL_VER=
set CODEQL_VER_MAJOR=
set CODEQL_VER_MINOR=
set CODEQL_VER_BUILD=
set CODEQL_VER_SPEC=
set CODEQL_SARIF_FMT=sarifv2.1.0
set CODEQL_PACK_GET=
set CODEQL_PACK_DEL=
set CODEQL_PACK_OP_FAILED=
set WDK_DEV_SUPP_TOOLS_FAILED=
set CODEQL_FAILED=
set SDV_FAILED=

rem Parse arguments
:argloop
shift /2
if "%2"=="" goto argend
set ARG=%2
if "%ARG:~0,1%"=="/" set BUILD_COMMAND=%ARG%& goto :argloop

if /I "%ARG%"=="Debug" set BUILD_FLAVOR=Debug& goto :argloop
if /I "%ARG%"=="dbg" set BUILD_FLAVOR=Debug& goto :argloop
if /I "%ARG%"=="chk" set BUILD_FLAVOR=Debug& goto :argloop

if /I "%ARG%"=="amd64" set BUILD_ARCH=amd64& goto :argloop
if /I "%ARG%"=="64" set BUILD_ARCH=amd64& goto :argloop
if /I "%ARG%"=="x64" set BUILD_ARCH=amd64& goto :argloop
if /I "%ARG%"=="32" set BUILD_ARCH=x86& goto :argloop
if /I "%ARG%"=="x86" set BUILD_ARCH=x86& goto :argloop
if /I "%ARG%"=="ARM64" set BUILD_ARCH=ARM64& goto :argloop

rem WHCP_LEGACY is not necessary. Here for completeness and templating for WHCP_NEXT.
if /I "%ARG%"=="WHCP_LEGACY" set WHCP_LEVEL=WHCP_LEGACY& goto :argloop
if /I "%ARG%"=="WHCP_21H2" set WHCP_LEVEL=WHCP_LEGACY& goto :argloop
if /I "%ARG%"=="WHCP_21H2" set WHCP_LEVEL=WHCP_LEGACY& goto :argloop
rem WHCP_23H2 is a non-existent repo branch. Included here for completeness.
if /I "%ARG%"=="WHCP_23H2" set WHCP_LEVEL=WHCP_LEGACY& goto :argloop
rem WHCP_24H2 is the default. Set below during SDV builds only.
if /I "%ARG%"=="WHCP_24H2" set WHCP_LEVEL=WHCP_24H2& goto :argloop

rem Enable ANSI palette support
call :prepare_palette

rem Assume that this is target OS version and split off the tag
call :split_target_tag "%ARG%"

rem Verify that this target OS is supported and valid
for %%N in (%SUPPORTED_BUILD_SPECS%) do @(
  set T=%%N
  set CANDIDATE_SPEC=
  set FOUND_MATCH=

  for %%A in ("!T:_=" "!") do @(
    if /I %%A=="%TARGET%" (
      set CANDIDATE_SPEC=!T!!TAG!
    )
    for %%B in (%BUILD_TARGETS%) do @(
      if /I %%B==%%~A!TAG! (
        set FOUND_MATCH=1
      )
    )
  )

  if not "!FOUND_MATCH!"=="" (
    if not "!CANDIDATE_SPEC!"=="" (
      set BUILD_SPEC=!CANDIDATE_SPEC!
      goto :argloop
    )
  )
)

rem Silently exit if the build target could not be matched
rem
rem The reason for ignoring build target mismatch are projects
rem like NetKVM, viostor, and vioscsi, which build different
rem sln/vcxproj for different targets. Higher level script
rem does not have information about specific sln/vcproj and
rem platform bindings, therefore it invokes this script once
rem for each sln/vcproj to make it decide when the actual build
rem should be invoked.

goto :eof

rem Figure out which targets we're building
:argend
if "%BUILD_SPEC%"=="" (
  for %%B in (%BUILD_TARGETS%) do @(
    call :split_target_tag "%%B"
    for %%N in (%SUPPORTED_BUILD_SPECS%) do @(
      set T=%%N
      set BUILD_SPEC=
      for %%A in ("!T:_=" "!") do @(
        if /I %%A=="!TARGET!" (
          set BUILD_SPEC=!T!!TAG!
        )
      )
      if not "!BUILD_SPEC!"=="" (
        call :build_target !BUILD_SPEC!
      )
    )
  )
) else (
  call :build_target %BUILD_SPEC%
)
goto :eof

rem Figure out which archs we're building
:build_target
if "%BUILD_ARCH%"=="" (
  call :build_arch %1 x86
  call :build_arch %1 amd64
) else (
  call :build_arch %1 %BUILD_ARCH%
)
goto :eof

rem Invoke Visual Studio and CodeQL as needed...
:build_arch
setlocal
set BUILD_ARCH=%2
set TAG=
for /f "tokens=1 delims=_" %%T in ("%1") do @set TARGET_PROJ_CONFIG=%%T
for /f "tokens=2 delims=_" %%T in ("%1") do @set TARGET_PLATFORM=%%T
for /f "tokens=3 delims=_" %%T in ("%1") do @set TAG=%%T

if /I "!TAG!"=="SDV" (
  rem There is no 32-bit SDV build
  if %BUILD_ARCH%==x86 (
    goto :build_arch_skip
  )
  rem Check the SDV build suppression variable
  if not "%_BUILD_DISABLE_SDV%"=="" (
    echo Skipping %TARGET_PROJ_CONFIG% SDV build because _BUILD_DISABLE_SDV is set
    goto :build_arch_skip
  )
)

rem Compose build log file name
if "%BUILD_FLAVOR%"=="Debug" (
  set BUILD_LOG_FILE=buildchk
) else (
  set BUILD_LOG_FILE=buildfre
)
set BUILD_LOG_FILE=%BUILD_LOG_FILE%_%TARGET_PLATFORM%_%BUILD_ARCH%.log

if %BUILD_ARCH%==amd64 (
  set BUILD_ARCH=x64
)
set TARGET_VS_CONFIG="%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%|%BUILD_ARCH%"

rem We set up the Build Environment and get started...
echo.
call :clr_print %_c_Cyn% "Building : %BUILD_FILE%"
pushd %BUILD_DIR%
call "%~dp0SetVsEnv.bat" %TARGET_PROJ_CONFIG%

rem Split MSBuild version... and check if we are Cobalt EWDK [21H2] or Germanium EWDK [24H2]...
for /f "tokens=1,2,3 usebackq delims=." %%i in (`echo %%VSCMD_VER%%`) do @set VSCMD_VER_MAJOR=%%i && set VSCMD_VER_MINOR=%%j && set VSCMD_VER_BUILD=%%k
rem MSBuild v17.8 and above are at least Germanium EWDK, so WHCP_24H2, everything below v17.8 is WHCP_LEGACY
if %VSCMD_VER_MAJOR% GTR 16 (
  if %VSCMD_VER_MAJOR% EQU 17 (
    if not %VSCMD_VER_MINOR% LSS 8 (
      set VSCMD_WHCP_LEVEL=WHCP_24H2
    ) else (
      set VSCMD_WHCP_LEVEL=WHCP_LEGACY
    )
  )
  if %VSCMD_VER_MAJOR% GTR 17 (
    set VSCMD_WHCP_LEVEL=WHCP_24H2
  )
) else (
  set VSCMD_WHCP_LEVEL=WHCP_LEGACY
)

rem Check for x86 viosock libraries and build them if needed...
if "%BUILD_FILE%"=="virtio-win.sln" (
  set VIOSOCK_PREBUILD_X86_LIBS=1
)
if "%BUILD_FILE%"=="viosock.sln" (
  set VIOSOCK_PREBUILD_X86_LIBS=1
)
if "%VIOSOCK_PREBUILD_X86_LIBS%" EQU "1" (
  if %BUILD_ARCH%==x64 (
    if not exist "%BUILD_DIR%viosock\lib\x86\%TARGET%%BUILD_FLAVOR%\viosocklib.dll" (
      echo.
      call :clr_print %_c_Yel% "ATTENTION : Need to build x86 viosock libraries before building for amd64..."
      setlocal
      set VIRTIO_WIN_NO_ARM=1
      if "%BUILD_FILE%"=="virtio-win.sln" (
        pushd "%BUILD_DIR%viosock\lib"
      )
      if "%BUILD_FILE%"=="viosock.sln" (
        pushd "%BUILD_DIR%lib"
      )
      call ..\..\build\build.bat viosocklib.vcxproj %TARGET% x86
      if ERRORLEVEL 1 (
        set BUILD_FAILED=1
      )
      popd
      if "%BUILD_FAILED%" EQU "1" (
        goto :build_arch_done
      )
      call :clr_print %_c_Grn% "Successfully built the x86 viosock libraries."
      echo.
      call :clr_print %_c_Cyn% "Continuing with amd64 build..."
      endlocal
    )
  )
)

rem Split builds between Code Analysis and No-Analyis...
if /I "!TAG!"=="SDV" (
  echo.
  rem SDV is deprecated from Germanium EWDK and Windows 11 24H2. Making some allowances...
  rem First, we permit environment variable SKIP_SDV_ACTUAL to truly skip SDV whilst continuing with CodeQL, CA and DVL.
  if "%SKIP_SDV_ACTUAL%" EQU "1" (
    call :clr_print %_c_Yel% "SKIP_SDV_ACTUAL is SET" %_c_Wht% " : Skipping Static Driver Verifier for %BUILD_FILE%"
    echo Configuration ^: %TARGET_VS_CONFIG%
    echo.
  ) else (
    rem We only do SDV for Win10 targets.
    if "%TARGET%"=="Win10" (
      rem Permit the Build Environment to be reconfigured if we have been called from Germanium+ EWDK
      if not "%VSCMD_WHCP_LEVEL%"=="WHCP_LEGACY" (
        echo Reconfiguring Build Environment to use Cobalt [21H2] EWDK Build Tools for a working Static Driver Verifier...
        setlocal
        if not "%EWDK11_DIR%"=="" (
          rem set VSCMD_DEBUG=1
          set EXTERNAL_INCLUDE=
          set VS170COMNTOOLS=
          set WDKBuildBinRoot=
          set WDKToolRoot=
          set WindowsSdkBinPath=
          set WindowsTargetPlatformVersion=
          set __VSCMD_PREINIT_VCToolsVersion=
          set DevEnvDir=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\
          set INCLUDE=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\ATLMFC\include;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\include;%EWDK11_DIR%\Program Files\Windows Kits\10\include\shared;%EWDK11_DIR%\Program Files\Windows Kits\10\include\um;%EWDK11_DIR%\Program Files\Windows Kits\10\include\winrt;%EWDK11_DIR%\Program Files\Windows Kits\10\include\cppwinrt
          set LIB=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\ATLMFC\lib\x86;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\lib\x86;%EWDK11_DIR%\Program Files\Windows Kits\10\lib\um\x86
          set LIBPATH=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\ATLMFC\lib\x86;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\lib\x86;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\lib\x86\store\references;;C:\Windows\Microsoft.NET\Framework\v4.0.30319
          set Path=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\bin\HostX86\x86;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\VC\VCPackages;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\bin\Roslyn;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\devinit;x86;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\\MSBuild\Current\Bin;C:\Windows\Microsoft.NET\Framework\v4.0.30319;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\;%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\Git\cmd;C:\Users\Stripe\AppData\Local\Microsoft\WindowsApps;%EWDK11_DIR%\Program Files\Windows Kits\10\\bin\10.0.22000.0\x86;%EWDK11_DIR%\Program Files\Windows Kits\10\\Tools\bin\i386;%EWDK11_DIR%\Program Files\Windows Kits\10\\tools;%EWDK11_DIR%\Program Files\Windows Kits\10\\tools\x86;%EWDK11_DIR%\BuildEnv
          set VCIDEInstallDir=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\VC\
          set VCINSTALLDIR=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\
          set VCToolsInstallDir=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\
          set VCToolsRedistDir=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.29.30133\
          set VCToolsVersion=14.29.30133
          set VSINSTALLDIR=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\
          set __devinit_path=%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\devinit\devinit.exe
          call %EWDK11_DIR%\BuildEnv\SetupBuildEnv.cmd
          call "%~dp0SetVsEnv.bat" %TARGET_PROJ_CONFIG%
          echo.
        ) else (
          call :clr_print %_c_Yel% "The EWDK11_DIR environment variable is not set. Set this variable and try again."
          set BUILD_FAILED=1
          goto :build_arch_done
        )
      )
      call :clr_print %_c_Cyn% "Running Static Driver Verifier build for %BUILD_FILE%."
      echo Configuration ^: %TARGET_VS_CONFIG%
      echo.
      call :run_sdv "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
      if "%BUILD_FAILED%" EQU "1" (
        call :clr_print %_c_Red% "Static Driver Verifier BUILD FAILED."
        goto :build_arch_done
      )
      call :clr_print %_c_Grn% "Static Driver Verifier build for %BUILD_FILE% succeeded."
      echo.
      if not "%VSCMD_WHCP_LEVEL%"=="WHCP_LEGACY" (
        endlocal
      )
    ) else (
      call :clr_print %_c_Yel% "Skipping Static Driver Verifier for %BUILD_FILE%. SDV if for WHCP_LEGACY targets ONLY."
      echo Configuration ^: %TARGET_VS_CONFIG%
      echo.
    )
  )
  rem Here we start our CodeQL run if we find our CodeQL CLI binary...
  if exist "%CODEQL_BIN%" (
    if "%WHCP_LEVEL%"=="" (
      rem When no WHCP_LEVEL is provided, we presume analysis for WHCP_24H2.
      rem Specify the WHCP_LEGACY argument for pre-WHCP_24H2 analysis.
      rem Note: Technically Win10 should be WHCP_LEGACY but WHCP_24H2 still works.
      set WHCP_LEVEL=WHCP_24H2
    )
    echo CodeQL ^: Checking CodeQL environment...
    rem Get CodeQL verion...
    for /f "tokens=5,6,7 usebackq delims=. " %%i in (`%CODEQL_BIN% --version ^| findstr /B "CodeQL"`) do @(
      set CODEQL_VER=%%i.%%j.%%k
      set CODEQL_VER_MAJOR=%%i
      set CODEQL_VER_MINOR=%%j
      set CODEQL_VER_BUILD=%%k
    )
    rem Cater for any CodeQL CLI breaking changes by setting CODEQL_VER_SPEC
    if !CODEQL_VER_MAJOR! LSS 3 (
      if !CODEQL_VER_MINOR! EQU 15 (
        if !CODEQL_VER_BUILD! LEQ 3 (
          set CODEQL_VER_SPEC=CODEQL_TOO_OLD
        )
        if !CODEQL_VER_BUILD! EQU 4 (
          set CODEQL_VER_SPEC=CODEQL_MS_SPEC_V2_15_4
        )
        if !CODEQL_VER_BUILD! GTR 4 (
          set CODEQL_VER_SPEC=CODEQL_PRE_SARIF_MINIFY
        )        
      ) else (
        if !CODEQL_VER_MINOR! LSS 15 (
          set CODEQL_VER_SPEC=CODEQL_TOO_OLD
        ) else (
          if !CODEQL_VER_MINOR! EQU 16 (
            set CODEQL_VER_SPEC=CODEQL_PRE_SARIF_MINIFY
          ) else (
            if !CODEQL_VER_MINOR! EQU 17 (
              if !CODEQL_VER_BUILD! LEQ 4 (
                set CODEQL_VER_SPEC=CODEQL_PRE_SARIF_MINIFY
              ) else (
                set CODEQL_VER_SPEC=CODEQL_SARIF_MINIFY
              )
            ) else (
              if !CODEQL_VER_MINOR! GEQ 18 (
                set CODEQL_VER_SPEC=CODEQL_SARIF_MINIFY
              )
            )
          )
        )
      )
    ) else (
      set CODEQL_VER_SPEC=CODEQL_FUTURE
    )
    if not "!CODEQL_VER_SPEC!"=="CODEQL_TOO_OLD" (
      echo CodeQL ^: CodeQL CLI is version !CODEQL_VER!. Breaking change mitigation specification is !CODEQL_VER_SPEC!
      echo.
      call :clr_print %_c_Cyn% "CodeQL : Getting CodeQL suites for %BUILD_FILE%."
      echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
      echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
      call :get_codeql_suites
      if "!CODEQL_FAILED!" EQU "1" (
        set BUILD_FAILED=1
        call :clr_print %_c_Red% "CodeQL : BUILD FAILED" %_c_Wht% " - Resolve problem and try again."
        goto :build_arch_done
      )
      echo.
      call :clr_print %_c_Cyn% "CodeQL : Configuring CodeQL for %BUILD_FILE%."
      echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
      echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
      call :config_ql_whcp
      if "!CODEQL_FAILED!" EQU "1" (
        set BUILD_FAILED=1
        call :clr_print %_c_Red% "CodeQL : BUILD FAILED" %_c_Wht% " - Resolve problem and try again."
        goto :build_arch_done
      )
      echo.
      call :clr_print %_c_Cyn% "CodeQL : Performing CodeQL build for %BUILD_FILE%."
      echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
      echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
      call :run_ql "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
      if "!CODEQL_FAILED!" EQU "1" (
        set BUILD_FAILED=1
        call :clr_print %_c_Red% "CodeQL : BUILD FAILED" %_c_Wht% " - Resolve problem and try again."
        goto :build_arch_done
      )
      call :clr_print %_c_Grn% "CodeQL build for %BUILD_FILE% succeeded."
      echo.
    ) else (
      call :clr_print %_c_Yel% "CodeQL CLI is too old at version !CODEQL_VER!..!"
      set BUILD_FAILED=1
    )
  ) else (
      call :clr_print %_c_Yel% "CodeQL binary is missing!"
      @echo.
  )
  if "!BUILD_FAILED!" EQU "1" (
    call :clr_print %_c_Red% "CodeQL : BUILD FAILED" %_c_Wht% " - Resolve problem and try again."
    goto :build_arch_done
  )
  call :run_ca "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
  if "!BUILD_FAILED!" EQU "1" (
    call :clr_print %_c_Red% "Code Analysis BUILD FAILED."
    goto :build_arch_done
  )
  call :clr_print %_c_Grn% "Code Analysis build for %BUILD_FILE% succeeded."
  echo.
  call :run_dvl "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
  if "!BUILD_FAILED!" EQU "1" (
    call :clr_print %_c_Red% "Driver Verifier Log BUILD FAILED."
    goto :build_arch_done
  )
  call :clr_print %_c_Grn% "Driver Verifier Log build for %BUILD_FILE% succeeded."
  echo.
) else (
  rem Do a build without analysis.
  @echo.
  @echo Build File    ^= %BUILD_FILE%
  @echo Configuration ^= %TARGET_VS_CONFIG%
  @echo Command       ^= %BUILD_COMMAND%
  @echo.
  call :run_build "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
  if "!BUILD_FAILED!" EQU "1" (
    call :clr_print %_c_Red% "NO-ANALYSIS BUILD FAILED."
    goto :build_arch_done
  )
  call :clr_print %_c_Grn% "No-Analysis build for %BUILD_FILE% succeeded."
  echo.
)
:build_arch_done
popd
:build_arch_skip
if not "!BUILD_FAILED!"=="" (
  goto :fail
)
endlocal
goto :eof

:run_build
:: %1 - configuration (as "Win10 Release")
:: %2 - platform      (as x64)
:: %3 - build command (as "/Build")
set __TARGET__=%BUILD_COMMAND:/=%
::(n)ormal(d)etailed,(disg)nostic
set __VERBOSITY__=n
msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:%__TARGET__% /p:Configuration="%~1" /P:Platform=%2 -fileLoggerParameters:Verbosity=%__VERBOSITY__%;LogFile=%~dp1%BUILD_LOG_FILE%
if ERRORLEVEL 1 (
  set BUILD_FAILED=1
)
echo.
goto :eof

:run_sdv
if exist sdv (
  call :clr_print %_c_Cyn% "Removing previously created SDV artifacts..."
  rmdir /s /q sdv
  echo.
)

if "!SDV_FAILED!" NEQ "1" (
  call :clr_print %_c_Cyn% "Build: Cleaning for %BUILD_FILE%..."
  msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:clean /p:Configuration="%~1" /P:Platform=%2
  if ERRORLEVEL 1 (
    set SDV_FAILED=1
  )
  echo.
)
if "!SDV_FAILED!" NEQ "1" (
  call :clr_print %_c_Cyn% "Build: Cleaning SDV for %BUILD_FILE%..."
  msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:sdv /p:inputs="/clean" /p:Configuration="%~1" /P:platform=%2
  if ERRORLEVEL 1 (
    set SDV_FAILED=1
  )
  echo.
)
if "!SDV_FAILED!" NEQ "1" (
  call :clr_print %_c_Cyn% "Build: Performing SDV checks for %BUILD_FILE%..."
  msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:sdv /p:inputs="/check /devenv" /p:Configuration="%~1" /P:platform=%2
  if ERRORLEVEL 1 (
    set SDV_FAILED=1
  )
  echo.
)
if "!SDV_FAILED!" EQU "1" (
  set BUILD_FAILED=1
)
goto :eof

:run_ql
rem Prepare CodeQL build...
echo call "%~dp0SetVsEnv.bat" %~1 > %~dp1codeql.build.bat
echo msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:rebuild /p:Configuration="%~1" /P:Platform=%2 >> %~dp1codeql.build.bat

rem Create the CodeQL database...
call %CODEQL_BIN% database create -l=cpp -s=%~dp1 -c "%~dp1codeql.build.bat" %~dp1codeql_db -j 0
if ERRORLEVEL 1 (
  set CODEQL_FAILED=1
)

IF "%CODEQL_FAILED%" NEQ "1" (
  rem Do the analysis...
  rem CodeQL versions after v2.17.5 [2024-06-12] use a minified SARIF file. Here we make sure this does not occur.
  if "!CODEQL_VER_SPEC!"=="CODEQL_SARIF_MINIFY" (
    call %CODEQL_BIN% database analyze %~dp1codeql_db %CODEQL_DRIVER_SUITES%\windows_driver_recommended.qls --format=%CODEQL_SARIF_FMT% --output=%~dp1%BUILD_NAME%.sarif --no-sarif-minify -j 0
    if ERRORLEVEL 1 (
      set CODEQL_FAILED=1
    )
  ) else (
    call %CODEQL_BIN% database analyze %~dp1codeql_db %CODEQL_DRIVER_SUITES%\windows_driver_recommended.qls --format=%CODEQL_SARIF_FMT% --output=%~dp1%BUILD_NAME%.sarif -j 0
    if ERRORLEVEL 1 (
      set CODEQL_FAILED=1
    )
  )
)
goto :eof

:run_ca
call :clr_print %_c_Cyn% "Performing Code Analysis build of %BUILD_FILE%."
msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /p:Configuration="%~1" /P:Platform=%2 /P:RunCodeAnalysisOnce=True -fileLoggerParameters:LogFile=%~dp1%BUILD_NAME%.CodeAnalysis.log
if ERRORLEVEL 1 (
  set BUILD_FAILED=1
)
goto :eof

:run_dvl
call :clr_print %_c_Cyn% "Performing Driver Verfier Log build of %BUILD_FILE%."
msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:dvl /p:Configuration="%~1" /P:platform=%2
if ERRORLEVEL 1 (
  set BUILD_FAILED=1
)
goto :eof

:get_codeql_suites
echo CodeQL ^: Checking if the WDK Developer Supplemental Tools are available...
if exist "%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools" (
  if not "%CODEQL_RUN_BLIND%"=="" (
    call :clr_print %_c_Yel% "CodeQL : CODEQL_RUN_BLIND is set. Presuming WDK Developer Supplemental Tools are at the correct revision for desired CodeQL suites."
    goto :eof
  )
  echo CodeQL ^: Checking WDK Developer Supplemental Tools are at the correct revision for desired CodeQL suites...
  pushd "%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools"
  for /f "tokens=1 usebackq" %%i in (`git show -s head --format^=format:%%H`) do @set CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH=%%i
  echo CodeQL ^: WDK Developer Supplemental Tools are at commit hash !CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH!.
  if not "!CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH!"=="%CODEQL_DRIVER_SUITES_REPO_HASH%" (
    call :clr_print %_c_Yel% "CodeQL : Incorrect revision of WDK Developer Supplemental Tools found. Updating to main branch..."
    if "%CODEQL_OFFLINE_ONLY%"=="" (
      git pull origin main 1> nul 2>&1
      IF ERRORLEVEL 1 (
        set WDK_DEV_SUPP_TOOLS_FAILED=1
        call :clr_print %_c_Yel% "CodeQL : Error updating WDK Developer Supplemental Tools to main branch."
      ) else (
        echo CodeQL ^: Reverting WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH%.
        git checkout %CODEQL_DRIVER_SUITES_REPO_HASH% 1> nul 2>&1
        IF ERRORLEVEL 1 (
          set WDK_DEV_SUPP_TOOLS_FAILED=1
          call :clr_print %_c_Yel% "CodeQL : Error setting WDK Developer Supplemental Tools to correct commit."
        ) else (
          call :clr_print %_c_Grn% "CodeQL : WDK Developer Supplemental Tools are at the correct revision for desired CodeQL suites."
        )
      )
    ) else (
      call :clr_print %_c_Yel% "CodeQL : ERROR        : Failed to update the WDK Developer Supplemental Tools as CODEQL_OFFLINE_ONLY is set."
      call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually update the WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH% and try again."
      set WDK_DEV_SUPP_TOOLS_FAILED=1
      goto :get_codeql_suites_fail
    )
  ) else (
    call :clr_print %_c_Grn% "CodeQL : WDK Developer Supplemental Tools are at the correct revision for desired CodeQL suites."
  )
) else (
  if not "%CODEQL_OFFLINE_ONLY%"=="" (
    call :clr_print %_c_Yel% "CodeQL : ERROR        : CODEQL_OFFLINE_ONLY is set and the WDK Developer Supplemental Tools were NOT found."
    call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually install the WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH% and try again."
    set WDK_DEV_SUPP_TOOLS_FAILED=1
    goto :get_codeql_suites_fail
  )
  if not "%CODEQL_RUN_BLIND%"=="" (
    call :clr_print %_c_Yel% "CodeQL : ERROR        : CODEQL_RUN_BLIND is set and the WDK Developer Supplemental Tools were NOT found."
    call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually install the WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH% and try again."
    set WDK_DEV_SUPP_TOOLS_FAILED=1
    goto :get_codeql_suites_fail
  )
  call :clr_print %_c_Yel% "CodeQL : WDK Developer Supplemental Tools are NOT present. Cloning..."
  pushd "%CODEQL_HOME%"
  git clone https://github.com/microsoft/Windows-Driver-Developer-Supplemental-Tools.git --recursive -b main 1> nul 2>&1
  IF ERRORLEVEL 1 (
    set WDK_DEV_SUPP_TOOLS_FAILED=1
    call :clr_print %_c_Yel% "CodeQL : Error cloning the WDK Developer Supplemental Tools."
  ) else (
    pushd "%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools"
    echo CodeQL ^: Reverting WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH%.
    git checkout %CODEQL_DRIVER_SUITES_REPO_HASH% 1> nul 2>&1
    IF ERRORLEVEL 1 (
      set WDK_DEV_SUPP_TOOLS_FAILED=1
      call :clr_print %_c_Yel% "CodeQL : Error setting WDK Developer Supplemental Tools to correct commit."
    ) else (
      call :clr_print %_c_Grn% "CodeQL : WDK Developer Supplemental Tools are now at the correct revision for desired CodeQL suites."
    )
    popd
  )
)
:get_codeql_suites_fail
popd
IF "!WDK_DEV_SUPP_TOOLS_FAILED!" EQU "1" (
  set CODEQL_FAILED=1
)
goto :eof

:config_ql_whcp
if exist %~dp1codeql_db (
  echo CodeQL ^: Removing previously created rules database...
  rmdir /s /q %~dp1codeql_db
)

rem Make sure the codeql-workspace.yml file is removed [renamed] from the repo
rem so that CodeQL ignores the repo and relies on the cache instead
if exist "%CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml" (
  echo CodeQL ^: Renaming YAML mapping file so we use the CodeQL cache and not the repo...
  rem del %CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml
  ren %CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml codeql-workspace.yml.bak
)

rem Unfortunately, use of --model-packs=<name@range> [introduced in v2.17.5 2024-06-12] 
rem is ignored and the most recent pack in the cache is always used.
rem Therefore, we must prepare the cache depending on the WHCP_LEVEL variable.
rem From CodeQL CLI v2.15.4 on we use :
rem   [a] microsoft/windows-drivers@1.0.13 for everything below WHCP_24H2
rem   [b] microsoft/windows-drivers@1.1.0 for WHCP_24H2 [and potentially newer WHCP configs once released]
rem   [c] codeql/cpp-queries@0.9.0 for all configurations [and potentially newer WHCP configs once released]
rem NOTE    : This should also work with more recent versions of CodeQL CLI [tested to v2.19.3]
rem WARNING : Setting CODEQL_OFFLINE_ONLY together with WHCP_LEGACY will result in loss of WHCP_24H2 packages.
if "%WHCP_LEVEL%"=="WHCP_LEGACY" (
  set CODEQL_PACK_DEL=microsoft/windows-drivers@1.1.0
  set CODEQL_PACK_GET=codeql/cpp-queries@0.9.0,microsoft/windows-drivers@1.0.13
)
if "%WHCP_LEVEL%"=="WHCP_24H2" (
  set CODEQL_PACK_DEL=
  set CODEQL_PACK_GET=codeql/cpp-queries@0.9.0,microsoft/windows-drivers@1.1.0
)
if "%WHCP_LEVEL%"=="WHCP_NEXT" (
  rem USE THIS AS A TEMPLATE FOR FUTURE VERSIONS
  rem NOTE: Remember to populate the CODEQL_PACK_DEL variable for all WHCP_LEVELS above with __NEXTRANGE__ packs [per WHCP_LEGACY example]...
  set CODEQL_PACK_DEL=
  set CODEQL_PACK_GET=codeql/cpp-queries@__NEXTRANGE__,microsoft/windows-drivers@__NEXTRANGE__
)
if not "%CODEQL_RUN_BLIND%"=="" (
  call :clr_print %_c_Yel% "CodeQL : Skipping checking of CodeQL package cache as CODEQL_RUN_BLIND is set."
  goto :eof
)
echo CodeQL ^: Configuring CodeQL package cache for the !WHCP_LEVEL! configuration...
call :ql_pack_ops
goto :eof

:ql_pack_ops
if "!CODEQL_PACK_DEL!" NEQ "" for /f "tokens=1,2,3,4,5* usebackq delims=, " %%i in (`echo !CODEQL_PACK_DEL!`) do @call :ql_pack_del %%i %%j,%%k,%%l,%%m
if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
  set CODEQL_FAILED=1
  goto :eof
)
if "!CODEQL_PACK_DEL!" NEQ "" (
  goto :ql_pack_ops
)
if "!CODEQL_PACK_GET!" NEQ "" for /f "tokens=1,2,3,4,5* usebackq delims=, " %%i in (`echo !CODEQL_PACK_GET!`) do @call :ql_pack_get %%i %%j,%%k,%%l,%%m
if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
  set CODEQL_FAILED=1
  goto :eof
)
if "!CODEQL_PACK_GET!" NEQ "" (
  goto :ql_pack_ops
)
goto :eof

:ql_pack_get
if "%1" NEQ "" (
  for /f "tokens=1,2,3 delims=/@" %%i in ("%1") do @(
    echo CodeQL ^: Checking if needed CodeQL pack ^(%%i/%%j@%%k^) is in the package cache...
    if not exist "%USERPROFILE%\.codeql\packages\%%i\%%j\%%k" (
      call :clr_print %_c_Yel% "CodeQL : The %%i/%%j@%%k CodeQL pack was NOT found in the package cache."
      if not "%CODEQL_OFFLINE_ONLY%"=="" (
        echo.
        call :clr_print %_c_Yel% "CodeQL : ERROR        : CODEQL_OFFLINE_ONLY is set and the %%i/%%j@%%k CodeQL pack is missing."
        call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually install the %%i/%%j@%%k CodeQL pack and try again."
        set CODEQL_PACK_OP_FAILED=1
        set CODEQL_PACK_GET=
        goto :ql_pack_get_fail
      )
      call :clr_print %_c_Cyn% "CodeQL : Downloading the %%i/%%j@%%k CodeQL pack."
      call %CODEQL_BIN% pack download -v %%i/%%j@%%k
      IF ERRORLEVEL 1 (
        set CODEQL_PACK_OP_FAILED=1
        set CODEQL_PACK_GET=
        call :clr_print %_c_Yel% "CodeQL : Error downloading the %%i/%%j@%%k CodeQL pack."
      ) else (
        call :clr_print %_c_Grn% "CodeQL : Successfully downloaded the %%i/%%j@%%k CodeQL pack."
      )
    ) else (
      call :clr_print %_c_Grn% "CodeQL : The %%i/%%j@%%k CodeQL pack is already in the cache."
    )
  )
)
if "%2"=="" (
  set CODEQL_PACK_GET=
) else (
  set CODEQL_PACK_GET=%2,%3,%4,%5
)
:ql_pack_get_fail
if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
  set CODEQL_PACK_GET=
)
goto :eof

:ql_pack_del
if "%1" NEQ "" (
  for /f "tokens=1,2,3 delims=/@" %%i in ("%1") do @(
    echo CodeQL ^: Checking if unwanted CodeQL pack ^(%%i/%%j@%%k^) is in the package cache...
    if exist "%USERPROFILE%\.codeql\packages\%%i\%%j\%%k" (
      call :clr_print %_c_Yel% "CodeQL : Found %%i/%%j@%%k in the CodeQL package cache."
      echo CodeQL ^: Removing %%i/%%j@%%k...
      rmdir /s /q "%USERPROFILE%\.codeql\packages\%%i\%%j\%%k"
      IF ERRORLEVEL 1 (
        call :clr_print %_c_Yel% "CodeQL : Error removing %%i/%%j@%%k."
        set CODEQL_PACK_OP_FAILED=1
      ) else (
        call :clr_print %_c_Cyn% "CodeQL : Removed %%i/%%j@%%k."
      )
    ) else (
      call :clr_print %_c_Grn% "CodeQL : The CodeQL package %%i/%%j@%%k was NOT found in the cache."
    )
  )
)
if "%2"=="" (
  set CODEQL_PACK_DEL=
) else (
  set CODEQL_PACK_DEL=%2,%3,%4,%5
)
:ql_pack_del_fail
if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
  set CODEQL_PACK_DEL=
)
goto :eof

:split_target_tag
set TARGET=
set TAG=
for /f "tokens=1 delims=_" %%T in (%1) do @set TARGET=%%T
for /f "tokens=2 delims=_" %%T in (%1) do @set TAG=_%%T
goto :eof

:clr_print
@echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof

:prepare_palette
rem Colour mods should work from ABRACADABRA_WIN10_TH2
rem Get the ANSI ESC character [0x27]
for /f "tokens=2 usebackq delims=#" %%i in (`"prompt #$H#$E# & echo on & for %%i in (1) do rem"`) do @set z_esc=%%i
rem Prepare pallette
set "_c_Red="40;91m""
set "_c_Grn="40;92m""
set "_c_Yel="40;93m""
set "_c_Cyn="40;96m""
set "_c_Wht="40;37m""
goto :eof

:fail
exit /B 1
