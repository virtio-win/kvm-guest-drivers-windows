@echo off
setlocal
: Param1 - OS, Param2 - x86|amd64
: Param3 - file name
 
if /i "%1"=="Win7" goto :checkarch
if /i "%1"=="Wlh" goto :checkarch
if /i "%1"=="Wnet" goto :checkarch
if /i "%1"=="WXp" goto :checkarch
if /i "%1"=="Win8" goto :checkarch
if /i "%1"=="Win10" goto :checkarch
goto :printerr
:checkarch
if /i "%2"=="x86" goto :makeinstall
if /i "%2"=="amd64" goto :makeinstall
:printerr
echo wrong parameters (1)%1 (2)%2 (3)%3
goto :eof

:makeinstall
set INST_OS=%1
set INST_ARC=%2
set FILE_NAME=%3

if /i "%INST_ARC%"=="x86" set EXT_ARC=i386
if /i "%INST_ARC%"=="amd64" set EXT_ARC=amd64

set EXE_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%EXT_ARC%\%FILE_NAME%.exe
set PDB_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%EXT_ARC%\%FILE_NAME%.pdb

mkdir ..\Install\%INST_OS%\%INST_ARC%
if exist ..\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.* del /Q ..\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.*

echo %EXE_PATH_AND_NAME%
echo %PDB_PATH_AND_NAME%

copy /Y %EXE_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%
copy /Y %PDB_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%

set INST_OS=
set INST_ARC=
set FILE_NAME=
set EXE_PATH_AND_NAME=
set PDB_PATH_AND_NAME=

:eof
endlocal
