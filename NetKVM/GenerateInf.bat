@echo off
pushd "%~dp0"
if /i "%~n1"=="netkvm_no_RSS" call :netkvm_no_RSS
if /i "%~n1"=="netkvm_no_RSC" call :netkvm_no_RSC
if /i "%~n1"=="netkvm_no_USO" call :netkvm_no_USO
if /i "%~n1"=="netkvm" call :netkvm
popd
goto :eof
:netkvm_no_RSS
copy /y netkvm-base.txt netkvm_no_RSS.inx.tmp > nul
call :update netkvm_no_RSS.inx
goto :eof
:netkvm_no_RSC
copy /y netkvm-base.txt + netkvm-add-rss.txt netkvm_no_RSC.inx.tmp > nul
call :update netkvm_no_RSC.inx
goto :eof
:netkvm_no_USO
copy /y netkvm-base.txt + netkvm-add-rss.txt + netkvm-add-rsc.txt netkvm_no_USO.inx.tmp > nul
call :update netkvm_no_USO.inx
goto :eof
:netkvm
copy /y netkvm-base.txt + netkvm-add-rss.txt + netkvm-add-rsc.txt + netkvm-add-uso.txt netkvm.inx.tmp > nul
call :update netkvm.inx
goto :eof
:update
fc /b %1 %1.tmp >nul 2>&1
if errorlevel 1 copy /y %1.tmp %1 > nul
del %1.tmp
goto :eof
