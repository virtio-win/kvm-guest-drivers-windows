@ Echo off
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

Rem This script was made to run Static Driver Verifier.
Rem This script was Tested only on Windows 2008R2 and visual studio 2012.
Rem ver SdvTool 1.0.0.0

REM for running auto from build system use without any parameters.
SETLOCAL
IF "%~1"=="" ( SET var=1)
IF "%~1"=="" ( Goto PRERUN)

:Menu
cls
COLOR 0B
Echo.
Echo.= Menu ================= Ver 1.0.0.0 ===================
Echo.
Echo Please choose which operation would you like to Verify:
Echo.
Echo     (1): Verify     All Projects (ALL Configurations)
Echo     (2): Verify     NetKVM       (ALL Configurations)
Echo     (3): Verify     Vioserial    (ALL Configurations)
Echo     (4): Verify     Balloon      (ALL Configurations)
Echo     (5): Verify     Vioscsi      (ALL Configurations)
Echo     (6): Verify     Viostor      (ALL Configurations)
Echo     (7): Quit
Echo.
Echo ========================================================
Echo.
Echo Please choose which operation you would like to Verify,Then enter.
Echo E.g: 1 "Enter":
Echo.

SET /P var=[Choose Verify:]
Echo.
if  "%var%"=="7" (
    Goto End
    )
if "%var%"=="1" ( Goto PRERUN)
if "%var%"=="2" ( Goto PRERUN)
if "%var%"=="3" ( Goto PRERUN)
if "%var%"=="4" ( Goto PRERUN)
if "%var%"=="5" ( Goto PRERUN)
if "%var%"=="6" ( Goto PRERUN)

        Echo       *******************************************************************
        Echo       *     Wrong Operation Argument, Please Try Again in 5 sec ...     *
        Echo       *******************************************************************
        Ping -n 5 127.0.0.1 > NUL
        Goto Menu

:PRERUN
Rem  *******************************************************************
Rem                   Init all parameters
Rem  *******************************************************************
    SET ROOT_PATH=%~dp0
    SET all=
    SET err=0
    SET /A cnt=2
    SET parm1=
    SET parm2=
    SET parm3=
    SET parm4=
Rem  *******************************************************************

Rem  *******************************************************************
Rem                 Build VirtIO
Rem  *******************************************************************
COLOR
cd VirtIO
call buildAll.bat
cd ..
Rem  *******************************************************************
Rem             NetKVM Vioserial Balloon Vioscsi Viostor
Rem  *******************************************************************
:Var
if "%var%"=="1" ( Goto Process)
if "%var%"=="2" ( Goto NetKVM)
if "%var%"=="3" ( Goto Vioserial)
if "%var%"=="4" ( Goto Balloon)
if "%var%"=="5" ( Goto Vioscsi)
if "%var%"=="6" ( Goto Viostor)

Echo = Variables ============================================
Echo.
Rem  *****************************************************************************
Rem  Set PROJECT_XML_PATH, PROJECT_DIR_PATH PROJECT_NAME, ROOT_PATH for RunSdv.bat
Rem  *****************************************************************************

:NetKVM
SET PROJECT_XML_PATH=%ROOT_PATH%"NetKVM\NetKVM.vcxproj"
SET PROJECT_DIR_PATH=%ROOT_PATH%"NetKVM"
SET PROJECT_NAME="NetKVM"
Goto SetParam

:Vioserial
SET PROJECT_XML_PATH=%ROOT_PATH%"vioserial\sys\vioser.vcxproj"
SET PROJECT_DIR_PATH=%ROOT_PATH%"vioserial\sys"
SET PROJECT_NAME="vioser"
Goto SetParam

:Balloon
SET PROJECT_XML_PATH=%ROOT_PATH%"balloon\sys\balloon.vcxproj"
SET PROJECT_DIR_PATH=%ROOT_PATH%"balloon\sys"
SET PROJECT_NAME="balloon"
Goto SetParam

:Vioscsi
SET PROJECT_XML_PATH=%ROOT_PATH%"vioscsi\vioscsi.vcxproj"
SET PROJECT_DIR_PATH=%ROOT_PATH%"vioscsi"
SET PROJECT_NAME="vioscsi"
Goto SetParam

:Viostor
SET PROJECT_XML_PATH=%ROOT_PATH%"viostor\viostor.vcxproj"
SET PROJECT_DIR_PATH=%ROOT_PATH%"viostor"
SET PROJECT_NAME="viostor"
Goto SetParam

:SetParam
SET parm1=%ROOT_PATH%
SET parm2=%PROJECT_XML_PATH%
SET parm3=%PROJECT_DIR_PATH%
SET parm4=%PROJECT_NAME%
if  "%all%"=="true" GOTO IterateAll
Goto Process

Rem  *******************************************************************
Rem                        Start processing
Rem  *******************************************************************
:Process
if  "%var%"=="1" (
SET all=true
:p2
Goto Vioserial
:p3
Goto Balloon
:p4
Goto Viostor
:p5
Goto Vioscsi
:p6
Goto NetKVM
:IterateAll
    call RunSdv.bat %parm1% %parm2% %parm3% %parm4%
    IF %ERRORLEVEL% NEQ 0 SET err=1
    SET /A cnt+=1
    if  %cnt% GTR 6 GOTO END
    Goto  p%cnt%
    )

call RunSdv.bat %parm1% %parm2% %parm3% %parm4%
IF %ERRORLEVEL% NEQ 0 goto Error4
GOTO Menu

Rem  *******************************************************************
Rem                   Error handling
Rem  *******************************************************************

:Error1
    Echo ************************************************************************
    Echo *                 !!!!Wrong Project name!!!!                           *
    Echo ************************************************************************
    Echo.
    Ping -n 3 127.0.0.1 > NUL
    ENDLOCAL
    color
    EXIT /B 1

:Error2
    Echo *******************************************************************************
    Echo * Wrong Configuration, Couldn't find in project you chose, Please Try Again...*
    Echo *******************************************************************************
    Echo.
    Ping -n 3 127.0.0.1 > NUL
    ENDLOCAL
    color
    EXIT /B 1

:Error3
    Echo ***************************************************************************
    Echo *  Wrong Platform, There isn't in project you chose, Please Try Again ... *
    Echo ***************************************************************************
    Echo.
    Ping -n 3 127.0.0.1 > NUL
    ENDLOCAL
    color
    EXIT /B 1

:Error4
    Echo ***************************************************************************
    Echo *                       General Error, Please Try Again ...               *
    Echo ***************************************************************************
    Echo.
    Ping -n 3 127.0.0.1 > NUL
    ENDLOCAL
    color
    EXIT /B 1


Rem  *******************************************************************
Rem  Get user input for project name and validate it.
Rem  *******************************************************************
    :start
    SET /P parm1=[Please Choose project name  then Enter:]
    Echo Please wait,Validating Project Name...
    Ping -n 1 %parm1% | find "TTL" > NUL
    IF %ERRORLEVEL% NEQ 0 goto Error1

Rem  *******************************************************************
Rem  Get user input for Configuration Time
Rem  *******************************************************************
    :Next1
    SET /P parm2=[Please Choose Configuration then Enter:]
    Echo Please wait,Validating Project Name...
    Ping -n 1 %parm1% | find "TTL" > NUL
    IF %ERRORLEVEL% NEQ 0 goto Error2
    Goto Next2

Rem  *******************************************************************
Rem  Get user input for Platform
Rem  *******************************************************************
    :Next2
    SET /P parm3=[Please Choose Platform then Enter:]
    Echo Please wait,Validating Platform Name...
    Ping -n 1 %parm1% | find "TTL" > NUL
    IF %ERRORLEVEL% NEQ 0 goto Error3
    Goto Next3



Rem  *******************************************************************
Rem                   End and ReSET all parameters
Rem  *******************************************************************
:END
    color
    SET var=
    SET ans=
    SET parm1=
    SET parm2=
    SET parm3=
    SET parm4=
    SET var=
    SET all=
    SET cnt=
    Echo Thank you,Good Bye!
    COLOR
    ENDLOCAL
    Ping -n 5 127.0.0.1 > NUL
    EXIT /B %err%

