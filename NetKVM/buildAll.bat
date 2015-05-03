:: Build Windows 8
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win8 Release|Win32" /Out buildfre_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat 0x0620
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win8 Release|x64" /Out buildfre_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

rem setlocal
rem call tools\set_version.bat
rem call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win8 Debug|Win32"
rem endlocal
rem if %ERRORLEVEL% NEQ 0 goto :eof

rem setlocal
rem call tools\set_version.bat
rem call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win8 Debug|x64"
rem endlocal
rem if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 7
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win7 Release|Win32" /Out buildfre_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win7 Release|x64" /Out buildfre_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

rem setlocal
rem call tools\set_version.bat
rem call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win7 Debug|Win32"
rem endlocal
rem if %ERRORLEVEL% NEQ 0 goto :eof

rem setlocal
rem call tools\set_version.bat
rem call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Win7 Debug|x64"
rem endlocal
rem if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows Vista
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Vista Release|Win32" /Out buildfre_wlh_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 12 netkvm.sln /Rebuild "Vista Release|x64" /Out buildfre_wlh_amd64.log
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

call create_dvl_log.cmd
