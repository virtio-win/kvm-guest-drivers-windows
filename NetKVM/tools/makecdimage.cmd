@echo off
rem Parameters: 1 - name of resulting CD image file, 2 - name of directory to include files from

if /i exist "%~dp0\mkisofs.exe" goto usemkiso
if /i exist "%~dp0\cdimage.exe" goto usecdimage
rem add your favorite here
echo There is no utility for CD creation. You may find one in the Internet
echo No CD image (%1) created
goto :eof

:usemkiso
rem mkisofs from cdrtools
"%~dp0\mkisofs" -J -r -o %1 %2
goto :eof

:usecdimage
rem cdimage from MS
"%~dp0\cdimage.exe" -n %2 %1
goto :eof
