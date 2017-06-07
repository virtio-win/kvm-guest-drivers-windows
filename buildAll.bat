@echo off
call tools\build.bat virtio-win.sln "Wxp Wnet Wlh Win7 Win8 Win8.1 Win10" %*
if errorlevel 1 goto :fail
call tools\build.bat NetKVM\NetKVM-VS2015.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat vioscsi\vioscsi.vcxproj "Win8_SDV Win10_SDV" %*
if errorlevel 1 goto :fail
call tools\build.bat viostor\viostor.vcxproj "Win8_SDV Win10_SDV" %*
if errorlevel 1 goto :fail

for %%D in (pciserial fwcfg packaging Q35) do (
  pushd %%D
  call buildAll.bat
  if errorlevel 1 goto :fail
  popd
)

exit /B 0

:fail

exit /B 1
