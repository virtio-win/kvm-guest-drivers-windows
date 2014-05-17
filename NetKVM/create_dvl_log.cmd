setlocal
call tools\set_version.bat 0x0620
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x64
msbuild.exe netkvm.vcxproj /t:clean /p:Configuration="Win8 Release" /P:Platform=x64
msbuild.exe netkvm.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win8 Release" /P:platform=x64
msbuild.exe netkvm.vcxproj /p:Configuration="Win8 Release" /P:Platform=x64 /P:RunCodeAnalysisOnce=True
msbuild.exe netkvm.vcxproj /t:sdv /p:inputs="/check" /p:Configuration="Win8 Release" /P:platform=x64
msbuild.exe netkvm.vcxproj /t:dvl /p:Configuration="Win8 Release" /P:platform=x64
copy netkvm.DVL.XML .\Install\win8\amd64\
endlocal
