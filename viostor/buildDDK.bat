set DDKVER=6001.18001
set BUILDROOT=C:\WINDDK\%DDKVER%
pushd %BUILDROOT%
set X64ENV=x64
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre %1 %2
popd
build -cZg
