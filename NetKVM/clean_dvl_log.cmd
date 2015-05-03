setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x64
msbuild.exe netkvm.vcxproj /t:clean /p:Configuration="Win8 Release" /P:Platform=x64
msbuild.exe netkvm.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win8 Release" /P:Platform=x64
endlocal
