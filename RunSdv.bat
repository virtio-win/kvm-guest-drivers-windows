REM -----------------------------------------------------------------------
REM  Copyright (c) 2010-2016  Red Hat, Inc.
REM
REM  File: RunSdv.js
REM
REM  Author(s):
REM   Miki Mishael <mikim@daynix.com>
REM
REM  This work is licensed under the terms of the GNU GPL, version 2.  See
REM  the COPYING file in the top-level directory.
REM -----------------------------------------------------------------------
@echo off
cls
cls
Echo.
Echo          ***********************************************
Echo          *     Runnig SDV...  Don't close!             *
Echo          ***********************************************
:START

SETLOCAL
REM Set NATIVE ENV for running SDV Tool.
set vsVer=11
set arc=x86
call %~dp0Tools\SetVsEnv.bat %vsVer% %arc%
if %ERRORLEVEL% NEQ 0 goto Error3

SET ROOT_PATH=%1
SET PROJECT_XML_PATH=%2
SET PROJECT_DIR_PATH=%3
SET PROJECT_NAME=%4
title "DO Not Close!-Running SDV on %PROJECT_NAME% (ALL Configurations)"

if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x602
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%

cscript.exe %ROOT_PATH%\Tools\RunSdv.js  %ROOT_PATH% %PROJECT_XML_PATH% %PROJECT_DIR_PATH% %PROJECT_NAME% //Nologo
if %ERRORLEVEL% EQU 0 goto End
if %ERRORLEVEL% EQU 1 goto Error1
if %ERRORLEVEL% EQU 2 goto Error2

:Error2
Echo.
Echo       *********************************************************************
Echo       * Fatal Error Running SDV on %PROJECT_NAME%, Try Again, Good Bye... *
Echo       *********************************************************************
Echo.
ENDLOCAL
EXIT /B 1

REM Error in specific Configurations and platform
:Error1
Echo.
Echo       *********************************************************************
Echo       *  Error Running With Configurations and platform on %PROJECT_NAME% *
Echo       *********************************************************************
Echo.
ENDLOCAL
EXIT /B 1

:Error3
Echo.
Echo       *********************************************************************
Echo       *        Error Couldn't find VS instalation                         *
Echo       *********************************************************************
Echo.
ENDLOCAL
EXIT /B 1

:End
Echo.
Echo       **********************************************************
Echo       *  Successful Running SDV on this project %PROJECT_NAME% *
Echo       **********************************************************
Echo.
Ping -n 3 127.0.0.1 > NUL
:FIN
ENDLOCAL
EXIT /B 0