@echo off
call tools\build.bat virtio-win.sln Win10 ARM64
if errorlevel 1 goto :fail
call tools\build.bat virtio-win.sln "Wxp Wnet Wlh Win7 Win8 Win8.1 Win10" %*
if errorlevel 1 goto :fail
call tools\build.bat NetKVM\NetKVM-VS2015.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioscsi\vioscsi.vcxproj "Win8_SDV Win10_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viostor\viostor.vcxproj "Win8_SDV Win10_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat ivshmem\ivshmem.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :fail

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
findstr /v /c:"General.Checksum" "%~1" > "%~dpn1-compat%~x1"
goto :eof
