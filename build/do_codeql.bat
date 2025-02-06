@echo off
setlocal enabledelayedexpansion

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
rem
rem Define Minimum CodeQL Version
set CODEQL_MIN_VER=2.20.3
set CODEQL_MIN_VER_MAJOR=
set CODEQL_MIN_VER_MINOR=
set CODEQL_MIN_VER_BUILD=
rem 
rem Other variables
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
set CODEQL_GET_CLI_BINS=
set CODEQL_UPDATE_CLI_BINS=Yes

rem Check for CodeQL binaries...
if not exist "%CODEQL_BIN%" (
  call :clr_print %_c_Yel% "CodeQL : WARNING      : The CodeQL binaries are missing."
  if not "%CODEQL_GET_CLI_BINS%"=="" (
    if not "%CODEQL_RUN_BLIND%"=="" (
      call :clr_print %_c_Yel% "CodeQL : ERROR        : Failed to get the CodeQL binaries as CODEQL_RUN_BLIND is set."
      call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually install the CodeQL binaries to version %CODEQL_MIN_VER% and try again."
      endlocal
      exit /b 1
    )
    call :get_codeql_bins install
    if not "!errorlevel!"=="0" (
      exit /b !errorlevel!
    )
  ) else (
    endlocal
    exit /b 2
  )
)
call :clr_print %_c_Cyn% "CodeQL : Checking CodeQL environment..."
call :chk_vers
if not "!errorlevel!"=="0" (
  exit /b !errorlevel!
)
if "!CODEQL_VER_SPEC!"=="CODEQL_TOO_OLD" (
  call :clr_print %_c_Yel% "CodeQL CLI is too old at version %CODEQL_VER%. Minimum version is %CODEQL_MIN_VER%."
  goto :fail
)
call :clr_print %_c_Wht% "CodeQL : CodeQL CLI is version !CODEQL_VER!. Breaking change mitigation specification is !CODEQL_VER_SPEC!."
@echo.
call :clr_print %_c_Cyn% "CodeQL : Getting CodeQL suites for %BUILD_FILE%."
echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
call :get_codeql_suites
if "!CODEQL_FAILED!" EQU "1" (
  goto :fail
)
echo.
call :clr_print %_c_Cyn% "CodeQL : Configuring CodeQL for %BUILD_FILE%."
echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
call :config_ql_whcp
if "!CODEQL_FAILED!" EQU "1" (
  goto :fail
)
echo.
call :clr_print %_c_Cyn% "CodeQL : Performing CodeQL build for %BUILD_FILE%."
echo CodeQL ^: Configuration ^= %TARGET_VS_CONFIG%
echo CodeQL ^: WHCP_LEVEL    ^= !WHCP_LEVEL!
call :run_ql "%TARGET_PROJ_CONFIG% %BUILD_FLAVOR%" %BUILD_ARCH%
if "!CODEQL_FAILED!" EQU "1" (
  goto :fail
)
endlocal
goto :eof

:chk_vers
if not "%CODEQL_RUN_BLIND%"=="" (
  call :clr_print %_c_Yel% "CodeQL : WARNING      : NOT checking the version of CodeQL binaries as CODEQL_RUN_BLIND is set."
  set errorlevel=2
  goto :eof
)
rem Get installed CodeQL version parts...
for /f "tokens=5,6,7 usebackq delims=. " %%i in (`%CODEQL_BIN% --version ^| findstr /B "CodeQL"`) do @(
  set CODEQL_VER=%%i.%%j.%%k
  set CODEQL_VER_MAJOR=%%i
  set CODEQL_VER_MINOR=%%j
  set CODEQL_VER_BUILD=%%k
)
rem Get minimum CodeQL version parts...
for /f "tokens=1,2,3 usebackq delims=. " %%i in (`echo %CODEQL_MIN_VER%`) do @(
  set CODEQL_MIN_VER_MAJOR=%%i
  set CODEQL_MIN_VER_MINOR=%%j
  set CODEQL_MIN_VER_BUILD=%%k
)
rem Compare installed with minimum...
if not "%CODEQL_UPDATE_CLI_BINS%"=="" (
  if !CODEQL_VER_MAJOR! LSS !CODEQL_MIN_VER_MAJOR! (
    call :get_codeql_bins upgrade
    goto :chk_vers
  ) else (
    if !CODEQL_VER_MAJOR! EQU !CODEQL_MIN_VER_MAJOR! (
      if !CODEQL_VER_MINOR! LSS !CODEQL_MIN_VER_MINOR! (
        call :get_codeql_bins upgrade
        goto :chk_vers
      ) else (
        if !CODEQL_VER_MINOR! EQU !CODEQL_MIN_VER_MINOR! (
          if !CODEQL_VER_BUILD! LSS !CODEQL_MIN_VER_BUILD! (
            call :get_codeql_bins upgrade
            goto :chk_vers
          )
        )
      )
    )
  )
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
goto :eof

:get_codeql_bins
set install_op=%1
if not "%CODEQL_OFFLINE_ONLY%"=="" (
  call :clr_print %_c_Yel% "CodeQL : ERROR        : Failed to get the CodeQL binaries as CODEQL_OFFLINE_ONLY is set."
  call :clr_print %_c_Cyn% "CodeQL : RESOLUTION   : Manually %install_op% the CodeQL binaries to version %CODEQL_MIN_VER% and try again."
  set errorlevel=1
  goto :eof
)

if exist "%CODEQL_HOME%\codeql" (
  rd /s /q "%CODEQL_HOME%\codeql"
)
if not exist "%CODEQL_HOME%" (
  md "%CODEQL_HOME%"
)
pushd "%CODEQL_HOME%"
if "%install_op%"=="install" (
  call :clr_print %_c_Cyn% "CodeQL : Attempting to %install_op% CodeQL binaries."
)
if "%install_op%"=="upgrade" (
  call :clr_print %_c_Yel% "CodeQL : WARNING   : The CodeQL binaries are out-of-date: %CODEQL_VER%"
  call :clr_print %_c_Cyn% "CodeQL : Attempting to %install_op% CodeQL binaries to version %CODEQL_MIN_VER%."
)
echo Downloading CodeQL version %CODEQL_MIN_VER%...
curl -LO --ssl-no-revoke https://github.com/github/codeql-cli-binaries/releases/download/v%CODEQL_MIN_VER%/codeql-win64.zip
if not "%errorlevel%"=="0" (
  goto :fail
)
echo Unpacking CodeQL version %CODEQL_MIN_VER%...
powershell -command "Expand-Archive -Force 'codeql-win64.zip' '.'"
if not "%errorlevel%"=="0" (
  goto :fail
)
if "%install_op%"=="install" (
  call :clr_print %_c_Grn% "CodeQL : Successfully installed version %CODEQL_MIN_VER% of the CodeQL binaries."
)
if "%install_op%"=="upgrade" (
  call :clr_print %_c_Grn% "CodeQL : Successfully upgraded the CodeQL binaries to version %CODEQL_MIN_VER%."
)
popd
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
  for /f "tokens=1 usebackq" %%i in (`git show -s head --format^=format:%%H`) do @(
    set CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH=%%i
  )
  echo CodeQL ^: WDK Developer Supplemental Tools are at commit hash !CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH!.
  if not "!CODEQL_DRIVER_SUITES_LOCAL_REPO_HASH!"=="%CODEQL_DRIVER_SUITES_REPO_HASH%" (
    call :clr_print %_c_Yel% "CodeQL : Incorrect revision of WDK Developer Supplemental Tools found. Updating to main branch..."
    if "%CODEQL_OFFLINE_ONLY%"=="" (
      git pull origin main 1> nul 2>&1
      if not "%errorlevel%"=="0" (
        set WDK_DEV_SUPP_TOOLS_FAILED=1
        call :clr_print %_c_Yel% "CodeQL : Error updating WDK Developer Supplemental Tools to main branch."
      ) else (
        echo CodeQL ^: Reverting WDK Developer Supplemental Tools to commit %CODEQL_DRIVER_SUITES_REPO_HASH%.
        git checkout %CODEQL_DRIVER_SUITES_REPO_HASH% 1> nul 2>&1
        if not "%errorlevel%"=="0" (
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
  if not "%errorlevel%"=="0" (
    set WDK_DEV_SUPP_TOOLS_FAILED=1
    call :clr_print %_c_Yel% "CodeQL : Error cloning the WDK Developer Supplemental Tools."
  ) else (
    pushd "%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools"
    echo CodeQL ^: Reverting WDK Developer Supplemental Tools to commit !CODEQL_DRIVER_SUITES_REPO_HASH!.
    git checkout !CODEQL_DRIVER_SUITES_REPO_HASH! 1> nul 2>&1
    if not "%errorlevel%"=="0" (
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
IF "%WDK_DEV_SUPP_TOOLS_FAILED%" EQU "1" (
  set CODEQL_FAILED=1
)
goto :eof

:config_ql_whcp
if exist %~dp1codeql_db (
  call :clr_print %_c_Cyn% "CodeQL : Removing previously created rules database..."
  rmdir /s /q %~dp1codeql_db
)

rem Make sure the codeql-workspace.yml file is removed [renamed] from the repo
rem so that CodeQL ignores the repo and relies on the cache instead
if exist "%CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml" (
  echo CodeQL ^: Renaming YAML mapping file so we use the CodeQL cache and not the repo...
  rem del %CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml
  ren %CODEQL_DRIVER_SUITES%\..\codeql-workspace.yml codeql-workspace.yml.bak
)

rem Unfortunately, use of --model-packs=<name@range>, introduced in v2.17.5 2024-06-12
rem is ignored and the most recent pack in the cache is always used.
rem Therefore, we must prepare the cache depending on the WHCP_LEVEL variable.
rem From CodeQL CLI v2.15.4 on we use :
rem   [a] microsoft/windows-drivers@1.0.13 for everything below WHCP_24H2
rem   [b] microsoft/windows-drivers@1.1.0 for WHCP_24H2, and potentially newer WHCP configs once released
rem   [c] codeql/cpp-queries@0.9.0 for all configurations, and potentially newer WHCP configs once released
rem NOTE    - This should also work with more recent versions of CodeQL CLI, tested to v2.20.3
rem WARNING - Setting CODEQL_OFFLINE_ONLY together with WHCP_LEGACY will result in loss of WHCP_24H2 packages.
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
  rem NOTE - Remember to populate the CODEQL_PACK_DEL variable for all WHCP_LEVELS above with __NEXTRANGE__ packs, per WHCP_LEGACY example...
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
  rem CodeQL versions after v2.17.5, released 2024-06-12, use a minified SARIF file. Here we make sure this does not occur.
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

:ql_pack_ops
if "!CODEQL_PACK_DEL!" NEQ "" for /f "tokens=1,2,3,4,5* usebackq delims=, " %%i in (`echo !CODEQL_PACK_DEL!`) do @(
  call :ql_pack_del %%i %%j,%%k,%%l,%%m
  if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
    set CODEQL_FAILED=1
    goto :eof
  )
)
if "!CODEQL_PACK_DEL!" NEQ "" (
  goto :ql_pack_ops
)
if "!CODEQL_PACK_GET!" NEQ "" for /f "tokens=1,2,3,4,5* usebackq delims=, " %%i in (`echo !CODEQL_PACK_GET!`) do @(
  call :ql_pack_get %%i %%j,%%k,%%l,%%m
  if "%CODEQL_PACK_OP_FAILED%" EQU "1" (
    set CODEQL_FAILED=1
    goto :eof
  )
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

:clr_print
@echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof

:fail
endlocal
exit /b 1
goto :eof :: never hit
