setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
msbuild.exe NetKVM-VS2015.vcxproj /t:clean /p:Configuration="Win10 Release" /P:Platform=x64
msbuild.exe NetKVM-VS2015.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win10 Release" /P:platform=x64
endlocal
