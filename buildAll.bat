@echo off

if "%VIRTIO_WIN_NO_ARM%"=="" call tools\build.bat virtio-win.sln "Win10 Win11" ARM64
if errorlevel 1 goto :fail

call tools\build.bat virtio-win.sln "Win10 Win11" %*
if errorlevel 1 goto :fail
call tools\build.bat NetKVM\NetKVM-VS2015.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioscsi\vioscsi.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viostor\viostor.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
if "%VIRTIO_WIN_SDV_2022%"=="" goto :nosdv2022
call tools\build.bat Balloon\sys\balloon.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat fwcfg64\fwcfg.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat ivshmem\ivshmem.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat pvpanic\pvpanic\pvpanic.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viorng\viorng\viorng.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioserial\sys\vioser.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viosock\sys\viosock.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viosock\wsk\wsk.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viosock\viosock-wsk-test\viosock-wsk-test.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viofs\pci\viofs.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioinput\hidpassthrough\hidpassthrough.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioinput\sys\vioinput.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viomem\sys\viomem.vcxproj "Win11_SDV" %*
if errorlevel 1 goto :fail


:nosdv2022

path %path%;C:\Program Files (x86)\Windows Kits\10\bin\x86\
for %%D in (pciserial fwcfg packaging Q35) do (
  echo building also %%D
  pushd %%D
  call buildAll.bat
  if errorlevel 1 goto :fail
  popd
)

for /R %%f in (*.dvl.xml) do call :process_xml %%f

exit /B 0

:fail

exit /B 1

:process_xml
echo creating "%~dpn1-compat%~x1"
findstr /v /c:"General.Checksum" "%~1" | findstr /v /c:".Semmle." > "%~dpn1-compat%~x1"
goto :eof
