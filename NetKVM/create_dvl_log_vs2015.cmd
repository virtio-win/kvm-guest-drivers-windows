setlocal
call tools\set_version.bat 0x0620
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
msbuild.exe NetKVM-VS2015.vcxproj /t:clean /p:Configuration="Win10 Release" /P:Platform=x64
msbuild.exe NetKVM-VS2015.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win10 Release" /P:platform=x64
msbuild.exe NetKVM-VS2015.vcxproj /p:Configuration="Win10 Release" /P:Platform=x64 /P:RunCodeAnalysisOnce=True
msbuild.exe NetKVM-VS2015.vcxproj /t:sdv /p:inputs="/check" /p:Configuration="Win10 Release" /P:platform=x64
msbuild.exe NetKVM-VS2015.vcxproj /t:dvl /p:Configuration="Win10 Release" /P:platform=x64
copy NetKVM.DVL.XML .\Install\Win10\amd64\
endlocal
