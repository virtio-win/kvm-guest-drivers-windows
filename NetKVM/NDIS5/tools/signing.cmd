: Param1=x86 | amd64
: Param2=path to INF file (SYS must be is in the same place)
: Param3=version string to patch INF
: Param4=InfArch string

set _OSMASK_=
goto :sign

:create
rem This section should not be used
rem set _CERTSTORE_=NetKVMTemporaryCertStore
rem certmgr -del -all -s %_CERTSTORE_%
rem Makecert -r -pe -ss %_CERTSTORE_% -n "CN=NetKVMTemporaryCert" NetKVMTemporaryCert.cer
goto :eof

:sign
if /i "%1"=="x86" set _OSMASK_=XP_X86,Server2003_X86
if /i "%1"=="amd64" set _OSMASK_=XP_X64,Server2003_X64
if /i "%1"=="x64" set _OSMASK_=XP_X64,Server2003_X64
call :dosign %1 %2 %3 %4
goto :eof

:_stampinf
..\..\Tools\xdate.exe -u > timestamp.txt
set /p STAMPINF_DATE= < timestamp.txt
del timestamp.txt
stampinf -f %1 -v %2 -a %3
goto :eof

:dosign
echo system %1 
echo INF file %2
echo VERSION file %3
echo Target OS mask %_OSMASK_% 
call :_stampinf %2 %3 %4
inf2cat /driver:%~dp2 /os:%_OSMASK_% /uselocaltime
goto :eof
