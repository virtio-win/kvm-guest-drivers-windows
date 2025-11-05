@echo off

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

echo.
echo Processing DVL files to create Windows 10 COMPAT ^(WIN10_RS1 ^/ 1607^) version...
for /R %%f in (*.dvl.xml) do @(
  call :process_xml %%f
)
if "%found_dvl_xml%"=="" (
  echo WARNING ^: No DVL files were found.
) else (
  echo Processing of DVL files is complete.
)

goto :bld_success

:bld_success
echo.
echo SDV BUILD COMPLETED SUCCESSFULLY.
call :leave 0
goto :eof

:fail
echo SDV BUILD FAILED.
set BUILD_FAILED=
call :leave 1
goto :eof

:leave
exit /B %1


:process_xml
set found_dvl_xml=Yes
set "dvl_file=%~dpn1-compat%~x1"
if not exist "%dvl_file%" (
  call :fudge_xml %1
) else (
  rem Here we retain the Windows 10 version 1607, WIN10_RS1, build 14393 COMPAT DVL.
  echo The file already exists : %dvl_file%
)
goto :eof

:fudge_xml
rem Here we create a Windows 10 version 1607, WIN10_RS1, build 14393 COMPAT DVL.
echo Auto-magically creating : %dvl_file%
findstr /v /c:"General.Checksum" "%~1" | findstr /v /c:".Semmle." > "%dvl_file%"
goto :eof
