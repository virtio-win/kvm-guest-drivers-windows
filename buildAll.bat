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
for %%D in (pciserial fwcfg Q35 packaging) do (
  echo.
  call :_color_echo %_c_Cyn% "Building : %%D"
  pushd %%D
  call buildAll.bat
  if errorlevel 1 goto :fail
  call :_color_echo %_c_Grn% "Build for %%D succeeded."
  popd
)

echo.
call :_color_echo %_c_Cyn% "Processing DVL xml files..."
for /R %%f in (*.dvl.xml) do call :process_xml %%f
call :_color_echo %_c_Cyn% "All processing completed."
echo.
call :_color_echo %_c_Grn% "BUILD COMPLETED SUCCESSFULLY."
exit /B 0

:fail
call :_color_echo %_c_Red% "BUILD FAILED."
set BUILD_FAILED=
exit /B 1

:process_xml
if not exist "%~dpn1-compat%~x1" (
  call :fudge_xml %1
) else (
  rem NOTE: Here we retain the COMPAT version created by C:\DVL1903\dvl.exe
  call :_color_echo %_c_Grn% "The file already exists : %~dpn1-compat%~x1"
)
goto :eof

:fudge_xml
call :_color_echo %_c_Yel% "Auto-magically creating : %~dpn1-compat%~x1"
rem NOTE: Here we also have to remove the ..General.Checksum because we modded the file and changed it.
findstr /v /c:"General.Checksum" "%~1" | findstr /v /c:".Semmle." > "%~dpn1-compat%~x1"
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
