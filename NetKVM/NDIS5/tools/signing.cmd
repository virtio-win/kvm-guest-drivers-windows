: Param1=signXP 
: Param2=x86 | amd64
: Param3=path to INF file (SYS must be is in the same place) 
: Param4=version string to patch INF

set _OSMASK_=
if exist %BUILDROOT%\bin\SelfSign\signability.exe set USESIGNABILITY=old
if /i "%1"=="signXP" goto signXP%USESIGNABILITY%
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

:_stampinf
..\..\Tools\xdate.exe -u > timestamp.txt
set /p STAMPINF_DATE= < timestamp.txt
del timestamp.txt
stampinf -f %1 -v %2 -a %_BUILDARCH%.%_NT_TARGET_MAJ_ARCH%.%_NT_TARGET_MIN_ARCH%
goto :eof

:dosign
echo system %1 
echo INF file %2
echo VERSION file %3
echo Target OS mask %_OSMASK_% 
call :_stampinf %2 %3
inf2cat /driver:%~dp2 /os:%_OSMASK_%
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
call :_stampinf %2 %3
signability /driver:%~dp2 /auto /cat /os:%_OSMASK_%
taskkill /FI "WINDOWTITLE eq signability*"
goto :eof

