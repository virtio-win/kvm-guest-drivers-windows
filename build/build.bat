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
set BUILD_INFO=
set BUILD_FAILED=

rem We have version 24H2 epoch buildtime conditions
rem VSCMD_VER version splits:
set VSCMD_VER_MAJOR=
set VSCMD_VER_MINOR=
set VSCMD_VER_BUILD=
rem VSCMD_VER version specific buildtime vars
set VSCMD_WHCP_LEVEL=

rem Analysis Build specific variables
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
set BUILD_INFO=%1
set BUILD_ARCH=%2
set TAG=
for /f "tokens=1,2,3 delims=_" %%i in ("%BUILD_INFO%") do @(
  set TARGET_PROJ_CONFIG=%%i
  set TARGET_PLATFORM=%%j
  set TAG=%%k
)

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
call :clr_print %_c_Cyn% "Building : %BUILD_FILE%..."
echo.
pushd %BUILD_DIR%
call "%~dp0SetVsEnv.bat" %TARGET_PROJ_CONFIG%

rem Split MSBuild version... and check if we are Windows 11, 21H2 [22000] EWDK or Windows 11, 24H2 [26100] EWDK...
for /f "tokens=1,2,3 usebackq delims=." %%i in (`echo %%VSCMD_VER%%`) do @(
  set VSCMD_VER_MAJOR=%%i
  set VSCMD_VER_MINOR=%%j
  set VSCMD_VER_BUILD=%%k
)
rem MSBuild v17.8 and above are at least Windows 11, 24H2 [26100] EWDK, so WHCP_24H2, everything below v17.8 is WHCP_LEGACY
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

rem Check for any prerequisite x86 libraries and build them if needed...
call "%~dp0prebuild_x86_libs.bat" %BUILD_FILE% %BUILD_ARCH% "%BUILD_DIR%" %TARGET% %BUILD_FLAVOUR%
IF ERRORLEVEL 1 (
  set BUILD_FAILED=1
  goto :build_arch_done
)

rem Split builds between Code Analysis and No-Analyis...
if /I "!TAG!"=="SDV" (
  echo.
  rem SDV is deprecated from Windows 11 24H2, both in the EWDK and WHCP. Making some allowances...
  rem We only do SDV for Win10 targets.
  if "%TARGET%"=="Win10" (
    rem Permit the Build Environment to be reconfigured if we have been called from Windows 11, 24H2 [26100] EWDK and above
    if not "%VSCMD_WHCP_LEVEL%"=="WHCP_LEGACY" (
      call :clr_print %_c_Cyn% "Reconfiguring Build Environment to use Windows 11, 21H2 [22000] EWDK Build Tools for a working Static Driver Verifier..."
      echo.
      setlocal
      if not "%EWDK11_DIR%"=="" (
        call "%~dp0adhoc_21h2_bootstrap.bat" 
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
    call :run_sdv "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
    if "%BUILD_FAILED%" EQU "1" (
      call :clr_print %_c_Red% "Static Driver Verifier BUILD FAILED - Resolve problem and try again."
      goto :build_arch_done
    )
    call :clr_print %_c_Grn% "Static Driver Verifier build for %BUILD_FILE% succeeded."
    if not "%VSCMD_WHCP_LEVEL%"=="WHCP_LEGACY" (
      rem Return to calling EWDK...
      endlocal
    )
  ) else (
    call :clr_print %_c_Yel% "Skipping Static Driver Verifier for %BUILD_FILE%. SDV if for WHCP_LEGACY targets ONLY."
    echo Configuration ^: %TARGET_VS_CONFIG%
    echo.
  )
  if exist "%CODEQL_BIN%" (
    call :clr_print %_c_Cyn% "CodeQL : Performing CodeQL build for %BUILD_FILE%."
    echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
    call :run_ql "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
    if "!CODEQL_FAILED!" EQU "1" (
      set BUILD_FAILED=1
      call :clr_print %_c_Red% "CodeQL : BUILD FAILED" %_c_Wht% " - Resolve problem and try again."
      goto :build_arch_done
    )
    call :clr_print %_c_Grn% "CodeQL build for %BUILD_FILE% succeeded."
  ) else (
      call :clr_print %_c_Yel% "CodeQL binary is missing!"
      @echo.
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
  call :clr_print %_c_Cyn% "Performing No-Analysis build for %BUILD_FILE%."
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

if exist %~dp1codeql_db (
  call :clr_print %_c_Cyn% "CodeQL : Removing previously created rules database..."
  rmdir /s /q %~dp1codeql_db
)

rem Prepare CodeQL build...
echo call "%~dp0SetVsEnv.bat" %~1 > %~dp1codeql.build.bat
echo msbuild.exe -maxCpuCount %~dp1%BUILD_FILE% /t:rebuild /p:Configuration="%~1" /P:Platform=%2 >> %~dp1codeql.build.bat

rem Create the CodeQL database...
call %CODEQL_BIN% database create -l=cpp -s=%~dp1 -c "%~dp1codeql.build.bat" %~dp1codeql_db -j 0
if ERRORLEVEL 1 (
  set CODEQL_FAILED=1
)

IF "%CODEQL_FAILED%" NEQ "1" (
  call %CODEQL_BIN% database analyze %~dp1codeql_db %CODEQL_DRIVER_SUITES%\windows_driver_recommended.qls --format=sarifv2.1.0 --output=%~dp1%BUILD_NAME%.sarif -j 0
  if ERRORLEVEL 1 (
    set CODEQL_FAILED=1
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
IF ERRORLEVEL 1 (
  set BUILD_FAILED=1
)
goto :eof

:split_target_tag
set BUILD_INFO=%1
set TARGET=
set TAG=
for /f "tokens=1,2 delims=_" %%i in (%BUILD_INFO%) do @(
  set TARGET=%%i
  if not "%%j"=="" (
    set TAG=_%%j
  )
)
goto :eof

:clr_print
@echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof

:prepare_palette
rem Colour mods should work from WIN10_TH2
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
