: Param1=signWin7 | signVista | signXP | sigtn2K
: Param2=x86 | amd64
: Param3=path to INF file (SYS must be is in the same place) 
: Param4=version string to patch INF

::set SIGNTIMESTAMP=/t "http://timestamp.verisign.com/scripts/timestamp.dll"
set SIGNCERT=/f tools\NetKVMTemporaryCert.pfx /p password
set _OSMASK_=
if exist %BUILDROOT%\bin\SelfSign\signability.exe set USESIGNABILITY=old
if "%1"=="signVista" goto signVista%USESIGNABILITY%
if "%1"=="signXP" goto signXP%USESIGNABILITY%
if "%1"=="sign2K" goto sign2K%USESIGNABILITY%
if "%1"=="signWin7" goto signWin7%USESIGNABILITY%
echo unsupported parameter %1
goto :eof
:create
rem This section should not be used
rem set _CERTSTORE_=NetKVMTemporaryCertStore
rem certmgr -del -all -s %_CERTSTORE_%
rem Makecert -r -pe -ss %_CERTSTORE_% -n "CN=NetKVMTemporaryCert" NetKVMTemporaryCert.cer
goto :eof
:signXP
shift
if /i "%1"=="x86" set _OSMASK_=XP_X86,Server2003_X86
if /i "%1"=="amd64" set _OSMASK_=XP_X64,Server2003_X64
if /i "%1"=="x64" set _OSMASK_=XP_X64,Server2003_X64
call :dosign %1 %2 %3 
goto :eof

:signVista
shift
if /i "%1"=="x86" set _OSMASK_=Vista_X86,Server2008_X86,7_X86
if /i "%1"=="amd64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
if /i "%1"=="x64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
call :dosign %1 %2 %3 
goto :eof

:signWin7
shift
if /i "%1"=="x86" set _OSMASK_=Vista_X86,Server2008_X86,7_X86
if /i "%1"=="amd64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
if /i "%1"=="x64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
call :dosign %1 %2 %3 
goto :eof

:sign2K
shift
set _OSMASK_=2000
call :dosign %1 %2 %3 
goto :eof

:dosign
echo system %1 
echo INF file %2
echo VERSION file %3
echo Target OS mask %_OSMASK_% 
for /F "usebackq tokens=2" %%d in (`date /t`) do stampinf -f %2 -d %%d -v %3 -a %_BUILDARCH%.%_NT_TARGET_MAJ_ARCH%.%_NT_TARGET_MIN_ARCH%
inf2cat /driver:%~dp2 /os:%_OSMASK_%
SignTool sign %SIGNCERT% %SIGNTIMESTAMP%  %~dpn2.cat 
goto :eof

:signVistaold
shift
echo system %1 
echo INF file %2
echo VERSION file %3
set _OSMASK_=0
if /i "%1"=="x86" set _OSMASK_=256
if /i "%1"=="amd64" set _OSMASK_=512
if /i "%1"=="x64" set _OSMASK_=512
echo Target OS mask %_OSMASK_% 
for /F "usebackq tokens=2" %%d in (`date /t`) do stampinf -f %2 -d %%d -v %3 -a %_BUILDARCH%.%_NT_TARGET_MAJ_ARCH%.%_NT_TARGET_MIN_ARCH%
signability /driver:%~dp2 /auto /cat /dtc /os:%_OSMASK_%
taskkill /FI "WINDOWTITLE eq signability*"
SignTool sign %SIGNCERT% %SIGNTIMESTAMP%  %~dpn2.cat 
goto :eof

:signXPold
shift
echo system %1 
echo INF file %2
echo VERSION file %3
set _OSMASK_=0
rem (32+8=40) xp32 + 2003-32
if /i "%1"=="x86" set _OSMASK_=40
rem (128+16=144) xp64 + 2003-64
if /i "%1"=="amd64" set _OSMASK_=144
if /i "%1"=="x64" set _OSMASK_=144
echo Target OS mask %_OSMASK_% 
for /F "usebackq tokens=2" %%d in (`date /t`) do stampinf -f %2 -d %%d -v %3 -a %_BUILDARCH%.%_NT_TARGET_MAJ_ARCH%.%_NT_TARGET_MIN_ARCH%
signability /driver:%~dp2 /auto /cat /os:%_OSMASK_%
taskkill /FI "WINDOWTITLE eq signability*"
SignTool sign %SIGNCERT% %SIGNTIMESTAMP%  %~dpn2.cat 
goto :eof

:sign2Kold
shift
echo system %1 
echo INF file %2
echo VERSION file %3
set _OSMASK_=2
echo Target OS mask %_OSMASK_% 
for /F "usebackq tokens=2" %%d in (`date /t`) do stampinf -f %2 -d %%d -v %3
signability /driver:%~dp2 /auto /cat /os:%_OSMASK_%
taskkill /FI "WINDOWTITLE eq signability*"
SignTool sign %SIGNCERT% %SIGNTIMESTAMP%  %~dpn2.cat 
goto :eof
