
:: Build Windows Vista
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x86" /Out buildfre_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x64" /Out buildfre_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 7
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x86" /Out buildfre_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x64" /Out buildfre_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x86" /Out buildfre_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x64" /Out buildfre_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8.1
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x86" /Out buildfre_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x64" /Out buildfre_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 10
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x86" /Out buildfre_win10_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x64" /Out buildfre_win10_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: XP
pushd NDIS5
call buildall.bat XP
popd
if %ERRORLEVEL% NEQ 0 goto :eof
xcopy /S /I /Y NDIS5\Install\XP\x86 Install\XP\x86

:: XP64
pushd NDIS5
call buildall.bat XP64
popd
if %ERRORLEVEL% NEQ 0 goto :eof
xcopy /S /I /Y NDIS5\Install\XP\amd64 Install\XP\amd64

if "%_BUILD_DISABLE_SDV%" neq "" goto :eof
call create_dvl_log_vs2015.cmd
