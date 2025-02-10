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
for %%D in (pciserial fwcfg Q35) do @(
 call :bld_inf_drvr %%D
)
echo.
call :clr_print %_c_Cyn% "Processing DVL files to create Windows 10, version 1607, WIN10_RS1 COMPAT version..."
for /R %%f in (*.dvl.xml) do @(
  call :process_xml %%f
)
if "%found_dvl_xml%"=="" (
  call :clr_print %_c_Yel% "WARNING : No DVL files were found."
) else (
  call :clr_print %_c_Cyn% "Processing of DVL files is complete."
)
:bld_success
echo.
call :clr_print %_c_Grn% "BUILD COMPLETED SUCCESSFULLY."
call :leave 0
goto :eof

:fail
call :clr_print %_c_Red% "BUILD FAILED."
set BUILD_FAILED=
call :leave 1
goto :eof

:bld_inf_drvr
set inf_drv=%~1
echo.
call :clr_print %_c_Cyn% "Building : %inf_drv%"
echo.
pushd %inf_drv%
call buildAll.bat
if not errorlevel==0 (
  goto :fail
)
call :clr_print %_c_Grn% "Build for %inf_drv% succeeded."
popd
goto :eof

:process_xml
set found_dvl_xml=Yes
set "dvl_file=%~dpn1-compat%~x1"
if not exist "%dvl_file%" (
  call :fudge_xml %1
) else (
  rem Here we retain the Windows 10 version 1607, WIN10_RS1, build 14393 COMPAT DVL.
  call :clr_print %_c_Grn% "The file already exists : %dvl_file%"
)
goto :eof

:fudge_xml
rem Here we create a Windows 10 version 1607, WIN10_RS1, build 14393 COMPAT DVL.
call :clr_print %_c_Yel% "Auto-magically creating : %dvl_file%"
findstr /v /c:"General.Checksum" "%~1" | findstr /v /c:".Semmle." > "%dvl_file%"
goto :eof

:clr_print
echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof

:prepare_palette
rem Colour mods should work from WIN10_TH2
rem Get the ANSI ESC character: 0x27
for /f "tokens=2 usebackq delims=#" %%i in (`"prompt #$H#$E# & echo on & for %%i in (1) do rem"`) do @(
  set z_esc=%%i
)
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
endlocal
exit /B %1
