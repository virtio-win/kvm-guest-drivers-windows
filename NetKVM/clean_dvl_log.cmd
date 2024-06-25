setlocal
call ..\Tools\SetVsEnv.bat
rmdir /q /s sdv
msbuild.exe NetKVM-VS2015.vcxproj /t:clean /p:Configuration="Win10 Release" /P:Platform=x64
msbuild.exe NetKVM-VS2015.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win10 Release" /P:platform=x64
endlocal
