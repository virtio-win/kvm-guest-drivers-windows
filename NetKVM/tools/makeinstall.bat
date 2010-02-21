: Param1 - x86|amd64|ia64|x64|64
: Param2 - path to SYS file
: Param3 - path to INF file
: Param4 - version in x.x.x.x form
: Param5 - Win7 | Vista | XP | 2K 
 

if /i "%1"=="x86" goto makeinstall
if /i "%1"=="amd64" goto makeinstall
if /i "%1"=="x64" goto makeinstall
echo wrong parameters (1)%1 (2)%2 (3)%3
goto :eof

:makeinstall
echo makeinstall %1 %2 %3 %4 %5
mkdir Install\%5\%1
del /Q Install\%5\%1\*
copy /Y %2 Install\%5\%1
copy /Y %~dpn2.pdb Install\%5\%1
copy /Y %3 Install\%5\%1
if not exist Install\NetKVMTemporaryCert.cer copy /Y "%~dp0\NetKVMTemporaryCert.cer" Install
if not exist "%~dp0\NetKvmInstall.exe" goto skipinstall
if not exist Install\NetKvmInstall.exe copy /Y "%~dp0\NetKvmInstall.exe" Install
if not exist Install\InstallCertificate.bat copy /Y "%~dp0\InstallCertificate.bat" Install
if not exist Install\certmgr.exe copy /Y "%BUILDROOT%\bin\SelfSign\certmgr.exe" Install
:skipinstall
call "%~dp0\signing.cmd" sign%5 %1 Install\%5\%1\%~nx3 %4
