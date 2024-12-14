@echo off

if "%VIRTIO_WIN_NO_ARM%"=="" call build\build.bat virtio-win.sln "Win10 Win11" ARM64
if errorlevel 1 goto :fail

call build\build.bat virtio-win.sln "Win10 Win11" %*
if errorlevel 1 goto :fail
call build\build.bat NetKVM\NetKVM-VS2015.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat vioscsi\vioscsi.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viostor\viostor.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
if "%VIRTIO_WIN_SDV_2022%"=="" goto :nosdv2022
call build\build.bat Balloon\sys\balloon.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat fwcfg64\fwcfg.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat ivshmem\ivshmem.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat pvpanic\pvpanic\pvpanic.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viorng\viorng\viorng.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat vioserial\sys\vioser.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viosock\sys\viosock.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viosock\wsk\wsk.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viosock\viosock-wsk-test\viosock-wsk-test.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viofs\pci\viofs.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat vioinput\hidpassthrough\hidpassthrough.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat vioinput\sys\vioinput.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail
call build\build.bat viomem\sys\viomem.vcxproj "Win10_SDV Win11_SDV" %*
if errorlevel 1 goto :fail

:nosdv2022

call :prepare_palette

path %path%;C:\Program Files (x86)\Windows Kits\10\bin\x86\
for %%D in (pciserial fwcfg Q35 packaging) do @(
  echo.
  call :_color_echo %_c_Cyn% "Building : %%D"
  pushd %%D
  call buildAll.bat
  if errorlevel 1 goto :fail
  call :_color_echo %_c_Grn% "Build for %%D succeeded."
  popd
)

echo.
call :_color_echo %_c_Cyn% "Processing DVL files to create COMPAT (Redstone 1 / 1607) version..."
for /R %%f in (*.dvl.xml) do @call :process_xml %%f
if "%found_dvl_xml%"=="" (
  call :_color_echo %_c_Yel% "WARNING : No DVL files were found."
) else (
  call :_color_echo %_c_Cyn% "Processing of DVL files is complete."
)

rem Report SDV and DVL creation status...
for %%D in (NetKVM vioscsi viostor) do @call :check_sdv_dvl %%D

:bld_success
echo.
call :_color_echo %_c_Grn% "BUILD COMPLETED SUCCESSFULLY."
call :leave 0
goto :eof

:fail
call :_color_echo %_c_Red% "BUILD FAILED."
set BUILD_FAILED=
call :leave 1
goto :eof

:process_xml
set found_dvl_xml=Yes
if not exist "%~dpn1-compat%~x1" (
  call :fudge_xml %1
) else (
  rem NOTE: Here we retain the Windows 10 version 1607 (build 14393) COMPAT DVL.
  call :_color_echo %_c_Grn% "The file already exists : %~dpn1-compat%~x1"
)
goto :eof

:fudge_xml
call :_color_echo %_c_Yel% "Auto-magically creating : %~dpn1-compat%~x1"
rem NOTE: Here we create a Windows 10 version 1607 (build 14393) COMPAT DVL.
findstr /v /c:"General.Checksum" "%~1" | findstr /v /c:".Semmle." > "%~dpn1-compat%~x1"
goto :eof

:check_sdv_dvl
echo.
set drvr=%~1
set results_file="%~dpn1\%~1.legacy_dvl_result.txt"
set sdv_dir="%~dpn1\sdv"
call :_color_echo %_c_Cyn% "%drvr% : Checking if the SDV directory exists..."
if exist %sdv_dir% (
  call :_color_echo %_c_Grn% "%drvr% : SUCCESS : The SDV directory exists."
) else (
  call :_color_echo %_c_Yel% "%drvr% : WARNING : The SDV directory does NOT exist."
)
echo.
call :_color_echo %_c_Cyn% "%drvr% : Checking if the DVL build logged results..."
if exist "%results_file%" (
  call :_color_echo %_c_Cyn% "%drvr% : Reporting Driver Verification Log build results..."
  if exist "%~dpn1\%~1.DVL-win10-latest.XML" (
    call :_color_echo %_c_Grn% "%drvr% : SUCCESS : Latest Windows 10 DVL was created with the Cobalt EWDK."
  ) else (
    call :_color_echo %_c_Yel% "%drvr% : WARNING : Latest Windows 10 DVL was NOT created."
  )
  if exist "%~dpn1\%~1.DVL-win11-latest.XML" (
    call :_color_echo %_c_Grn% "%drvr% : SUCCESS : Latest Windows 11 DVL was created."
  ) else (
    call :_color_echo %_c_Yel% "%drvr% : WARNING : Latest Windows 11 DVL was NOT created."
  )
  for /f "tokens=1,2 usebackq delims=," %%i in (`type %%results_file%%`) do @call :check_dvl_result %%i %%j
  del /f "%results_file%"
) else (
  call :_color_echo %_c_Yel% "%drvr% : WARNING : No DVL build results were logged."
)
goto :eof

:check_dvl_result
set dvl_ver=%~1
set dvl_outcome=%~2
if "%dvl_ver%"=="%dvl_outcome%" (
  if "%dvl_ver%"=="1607" (
    call :_color_echo %_c_Grn% "%drvr% : SUCCESS : Windows 10 version %dvl_ver% DVL was created with the Redstone 1 EWDK."
  ) else (
    if "%dvl_ver%"=="1903" (
      call :_color_echo %_c_Grn% "%drvr% : SUCCESS : Windows 10 version %dvl_ver% DVL was created with the Titanium EWDK."
    ) else (
      call :_color_echo %_c_Grn% "%drvr% : SUCCESS : Windows 10 version %dvl_ver% DVL was created with an EWDK of the same version."
    )
  )
) else (
  if "%dvl_ver%"=="1607" (
    if "%dvl_outcome%"=="1903" (
      call :_color_echo %_c_Yel% "%drvr% : WARNING : Windows 10 version %dvl_ver% DVL was derived from an alternate DVL created with the Titanium EWDK."
    ) else (
      if "%dvl_outcome%"=="latest" (
        call :_color_echo %_c_Yel% "%drvr% : WARNING : Windows 10 version %dvl_ver% DVL was derived from an alternate DVL created with the Cobalt EWDK."
      )
    )
  )
  if "%dvl_outcome%"=="fail" (
    if "%dvl_ver%"=="1607" (
      call :_color_echo %_c_Red% "%drvr% : FAILURE : Windows 10 version %dvl_ver% DVL was NOT created. The %drvr%.DVL-compat.XML file will be missing."
    ) else (
      if "%dvl_ver%"=="1903" (
        call :_color_echo %_c_Red% "%drvr% : FAILURE : Windows 10 version %dvl_ver% DVL was NOT created. The %drvr%.DVL-win10.XML file will be missing."
      )
    )
  )
)
goto :eof

:_color_echo
echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
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

:leave
if exist ".\build_log.txt" (
  call schtasks /create /tn build_log_cleanup /tr "%comspec% /c %~dp0build\clean_build_log.bat" /sc ONCE /sd 01/01/1910 /st 00:00 1> nul 2>&1
  call schtasks /run /tn build_log_cleanup 1> nul 2>&1
)
exit /B %1
goto :eof :: never hit
