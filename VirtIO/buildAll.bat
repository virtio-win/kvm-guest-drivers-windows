@echo off
call ..\tools\build.bat VirtioLib.vcxproj "Wxp Wnet Wlh Win7 Win8 Win10" %*

pushd WDF
call buildAll.bat %*
popd
