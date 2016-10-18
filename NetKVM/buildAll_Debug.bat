
:: Build Windows Vista
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Debug|x86" /Out buildchk_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Debug|x64" /Out buildchk_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 7
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Debug|x86" /Out buildchk_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Debug|x64" /Out buildchk_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Debug|x86" /Out buildchk_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Debug|x64" /Out buildchk_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8.1
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Debug|x86" /Out buildchk_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Debug|x64" /Out buildchk_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 10
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Debug|x86" /Out buildchk_win10_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Debug|x64" /Out buildchk_win10_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: XP
pushd NDIS5
call buildAll.bat XPCHK
popd

:: XP64
pushd NDIS5
call buildAll.bat XP64CHK
popd

xcopy /y /q tools\NetKVMTemporaryCert.cer Install_Debug\NetKVM.cer*
