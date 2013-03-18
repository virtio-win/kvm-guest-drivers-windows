: Param1 - x86|amd64|ia64|x64|64
: Param2 - path to SYS file
: Param3 - path to INF file
: Param4 - version in x.x.x.x form
: Param5 - XP
: Param6 - Install | InstallChk | (or any other folder name)

set folder_name=%6

if /i "%1"=="x86" goto makeinstall
if /i "%1"=="amd64" goto makeinstall
if /i "%1"=="x64" goto makeinstall
echo wrong parameters (1)%1 (2)%2 (3)%3
goto :eof

:prepareinf
::original inf %1, copy to %2, define OS %3
echo processing %1 for %3
cl /nologo -DINCLUDE_TEST_PARAMS -D%3 /I. /EP %1 > %~nx1
cscript /nologo tools\cleanemptystrings.vbs %~nx1 > %2\%~nx1
del %~nx1
goto :eof

:makeinstall
echo makeinstall %1 %2 %3 %4 %5
mkdir %folder_name%\%5\%1
del /Q %folder_name%\%5\%1\*
copy /Y %2 %folder_name%\%5\%1
copy /Y %~dpn2.pdb %folder_name%\%5\%1
call :prepareinf %3 %folder_name%\%5\%1 %5
call "%~dp0\signing.cmd" sign%5 %1 %folder_name%\%5\%1\%~nx3 %4
