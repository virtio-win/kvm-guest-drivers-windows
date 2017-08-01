REM -----------------------------------------------------------------------
REM  Copyright (c) 2010-2017 Red Hat, Inc.
REM
REM  Author(s):
REM   Miki Mishael <mikim@daynix.com>
REM
REM  Redistribution and use in source and binary forms, with or without
REM  modification, are permitted provided that the following conditions
REM  are met :
REM  1. Redistributions of source code must retain the above copyright
REM     notice, this list of conditions and the following disclaimer.
REM  2. Redistributions in binary form must reproduce the above copyright
REM     notice, this list of conditions and the following disclaimer in the
REM     documentation and / or other materials provided with the distribution.
REM  3. Neither the names of the copyright holders nor the names of their contributors
REM     may be used to endorse or promote products derived from this software
REM     without specific prior written permission.
REM  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
REM  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
REM  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
REM  ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
REM  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
REM  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREM ENT OF SUBSTITUTE GOODS
REM  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
REM  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
REM  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
REM  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
REM  SUCH DAMAGE.
REM -----------------------------------------------------------------------
@echo off
cls
cls
Echo.
Echo          ***********************************************
Echo          *     Running SDV...  Don't close!             *
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

set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
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
Echo       *        Error Couldn't find VS installation                        *
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
