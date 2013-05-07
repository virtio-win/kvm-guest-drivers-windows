@echo off
if "%DDKVER%"=="" set DDKVER=7600.16385.1
set BUILDROOT=C:\WINDDK\%DDKVER%

set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %2 fre %1 no_oacr
popd
build -cZg

set DDKVER=
set BUILDROOT=


