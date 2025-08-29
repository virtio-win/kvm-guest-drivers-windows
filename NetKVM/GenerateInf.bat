@echo off
pushd "%~dp0"
if /i "%~n1"=="netkvm_no_USO" call :netkvm_no_USO
if /i "%~n1"=="netkvm" call :netkvm
if /i "%~n1"=="netkvmpoll" call :netkvmpoll
popd
goto :eof
:netkvm_no_USO
copy /y netkvm-base.txt netkvm_no_USO.inx.tmp > nul
call :update netkvm_no_USO.inx
goto :eof
:netkvmpoll
copy /y netkvm-base.txt + netkvm-add-uso.txt + netkvm-add-poll.txt netkvmpoll.inx.tmp > nul
call :update netkvmpoll.inx
goto :eof
:netkvm
copy /y netkvm-base.txt + netkvm-add-uso.txt netkvm.inx.tmp > nul
call :update netkvm.inx
goto :eof
:update
fc /b %1 %1.tmp >nul 2>&1
if errorlevel 1 copy /y %1.tmp %1 > nul
del %1.tmp
goto :eof
