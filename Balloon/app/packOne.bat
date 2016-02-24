@echo off
: Param1 - Win10 | Win8 | Win7 | WLH | Wnet | XP
: Param2 - x86|x64
: Param3 - file name
 
if "%2"=="x64" set %%2=amd64

if /i "%1"=="WXP" goto :checkarch_old
if /i "%1"=="WNet" goto :checkarch_old
if /i "%1"=="WLH" goto :checkarch_old

if /i "%1"=="Win7" goto :checkarch
if /i "%1"=="Wlh" goto :checkarch
if /i "%1"=="Wnet" goto :checkarch
if /i "%1"=="WXp" goto :checkarch
goto :printerr

:checkarch_old

set PREFX=

goto :makeinstall

:checkarch
set PREFX=Release
goto :makeinstall

:printerr
echo wrong parameters (1)%1 (2)%2 (3)%3
goto :eof

:makeinstall
set INST_OS=%1
set INST_ARC=%2
set FILE_NAME=%3
rem set INST_EXT=INST_ARC

if /i "%INST_ARC%"=="x64" goto :set_x64

set INST_ARC=x86
if /i "%PREFX%"=="" set PREFX=objfre_%1_%2
goto :startcopy

:set_x64
set INST_ARC=amd64
set INST_EXT=amd64
if /i "%PREFX%"=="" set PREFX=objfre_%1_%INST_ARC%
:startcopy

set EXE_PATH_AND_NAME=%PREFX%\%INST_EXT%\%FILE_NAME%.exe
set PDB_PATH_AND_NAME=%PREFX%\%INST_EXT%\%FILE_NAME%.pdb

mkdir ..\Install\%INST_OS%\%INST_ARC%
del /Q ..\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.*

copy /Y %EXE_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%
copy /Y %PDB_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%

set INST_OS=
set INST_ARC=
set INST_EXT=
set FILE_NAME=
set EXE_PATH_AND_NAME=
set PDB_PATH_AND_NAME=
set PREFX=

