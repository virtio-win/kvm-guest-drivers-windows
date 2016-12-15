setlocal
: Param1 - OS, Param2 - x86|amd64
: Param3 - file name
 
if /i "%1"=="Win10" goto :checkarch
if /i "%1"=="Win8" goto :checkarch
if /i "%1"=="Win7" goto :checkarch
if /i "%1"=="Wlh" goto :checkarch
if /i "%1"=="Wnet" goto :checkarch

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
set SYS_NAME=%3
if /i "%INST_ARC%"=="x86" set INST_EXT=i386
if /i "%INST_ARC%"=="amd64" set INST_EXT=amd64

set SYS_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.sys
set PDB_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.pdb
set INF_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.inf

echo makeinstall %1 %2 %3
mkdir .\Install\%INST_OS%\%INST_ARC%
if exist .\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.* del /Q .\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.*
copy /Y %SYS_PATH_AND_NAME% .\Install\%INST_OS%\%INST_ARC%
copy /Y %PDB_PATH_AND_NAME% .\Install\%INST_OS%\%INST_ARC%
copy /Y %INF_PATH_AND_NAME% .\Install\%INST_OS%\%INST_ARC%\%SYS_NAME%.inf

:create_cat
echo "Setting OS mask for:" %1 %2

if /i "%1"=="win10" goto create_win10
if /i "%1"=="wlh" goto create_vista
if /i "%1"=="win7" goto create_win7
if /i "%1"=="win8" goto create_win8
if /i "%1"=="wnet" goto create_wnet

goto error_inf2cat

:create_wnet
if /i "%2"=="x86" set _OSMASK_=Server2003_X86
if /i "%2"=="amd64" set _OSMASK_=Server2003_X64
goto run_inf2cat

:create_vista
if /i "%2"=="x86" set _OSMASK_=Vista_X86,Server2008_X86,7_X86
if /i "%2"=="amd64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
goto run_inf2cat

:create_win7
if /i "%2"=="x86" set _OSMASK_=Vista_X86,Server2008_X86,7_X86
if /i "%2"=="amd64" set _OSMASK_=Vista_X64,Server2008_X64,7_X64,Server2008R2_X64
goto run_inf2cat

:create_win8
if /i "%2"=="x86" set _OSMASK_=8_X86
if /i "%2"=="amd64" set _OSMASK_=8_X64,Server8_X64
goto run_inf2cat

:create_win10
if /i "%2"=="x86" set _OSMASK_=10_X86
if /i "%2"=="amd64" set _OSMASK_=10_X64,Server10_X64
goto run_inf2cat

:error_inf2cat 
echo "Error setting OS mask for inf2cat"
goto after_inf2cat

:run_inf2cat
inf2cat /driver:Install\%INST_OS%\%INST_ARC% /os:%_OSMASK_%
:after_inf2cat
