@echo off

setlocal

if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x602
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

rem set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 

REM Create the include file for the driver's resource file.
rem call :create_version_file "viorng\2012-defines.h"
rem call :create_version_file "cng\um\2012-defines.h"

mkdir Install

rem for %%O in (Win7 Win8) do for %%P in (Win32 x64) do call ..\tools\callVisualStudio.bat 11 viorng.sln /Rebuild "%%O Release|%%P"

rem for %%P in (Win32 x64) do call ..\tools\callVisualStudio.bat 11 viorngum.sln /Rebuild "Release|%%P"
rem for %%O in (Win7 Win8) do copy "Win32\Release\viorngum.dll" "Install\%%O\x86\"
rem for %%O in (Win7 Win8) do copy "x64\Release\viorngum.dll" "Install\%%O\x64\"

for %%A in (Win7 Win8) do for %%B in (32 64) do call :%%A_%%B

endlocal

goto :eof

:WIN7_32
setlocal

set _NT_TARGET_VERSION=0x601
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call :create_version_file "viorng\2012-defines.h"
call :create_version_file "cng\um\2012-defines.h"

call ..\tools\callVisualStudio.bat 11 .\viorng\viorng.vcxproj /Rebuild "Win7 Release|Win32" /Out .\viorng\buildfre_win7_x86.log
mkdir .\Install\Win7\x86
copy .\viorng\objfre_win7_x86\i386\viorng.sys .\Install\Win7\x86
copy .\viorng\objfre_win7_x86\i386\viorng.inf .\Install\Win7\x86
copy .\viorng\objfre_win7_x86\i386\viorng.pdb .\Install\Win7\x86
copy "C:\Program Files (x86)\Windows Kits\8.0\Redist\wdf\x86\WdfCoInstaller01011.dll" .\Install\Win7\x86

call ..\tools\callVisualStudio.bat 11 .\cng\um\viorngum.vcxproj /Rebuild "Release|Win32" /Out .\cng\um\buildfre_win7_x86.log
copy .\cng\um\Win32\Release\viorngum.dll .\Install\Win7\x86
copy .\cng\um\Win32\Release\viorngum.pdb .\Install\Win7\x86

call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x86
inf2cat /driver:.\Install\Win7\x86 /os:Vista_X86,Server2008_X86,7_X86

endlocal
goto :eof

:WIN7_64
setlocal

set _NT_TARGET_VERSION=0x601
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call :create_version_file "viorng\2012-defines.h"
call :create_version_file "cng\um\2012-defines.h"

call ..\tools\callVisualStudio.bat 11 .\viorng\viorng.vcxproj /Rebuild "Win7 Release|x64" /Out .\viorng\buildfre_win7_amd64.log
mkdir .\Install\Win7\amd64
copy .\viorng\objfre_win7_amd64\amd64\viorng.sys .\Install\Win7\amd64
copy .\viorng\objfre_win7_amd64\amd64\viorng.inf .\Install\Win7\amd64
copy .\viorng\objfre_win7_amd64\amd64\viorng.pdb .\Install\Win7\amd64
copy "C:\Program Files (x86)\Windows Kits\8.0\Redist\wdf\x64\WdfCoInstaller01011.dll" .\Install\Win7\amd64

call ..\tools\callVisualStudio.bat 11 .\cng\um\viorngum.vcxproj /Rebuild "Release|x64" /Out .\cng\um\buildfre_win7_amd64.log
copy .\cng\um\x64\Release\viorngum.dll .\Install\Win7\amd64
copy .\cng\um\x64\Release\viorngum.pdb .\Install\Win7\amd64

call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x64
inf2cat /driver:.\Install\Win7\amd64 /os:Vista_X64,Server2008_X64,7_X64,Server2008R2_X64

endlocal
goto :eof

:WIN8_32
setlocal

set _NT_TARGET_VERSION=0x602
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call :create_version_file "viorng\2012-defines.h"
call :create_version_file "cng\um\2012-defines.h"

call ..\tools\callVisualStudio.bat 11 .\viorng\viorng.vcxproj /Rebuild "Win8 Release|Win32" /Out .\viorng\buildfre_win8_x86.log
mkdir .\Install\Win8\x86
copy .\viorng\objfre_win8_x86\i386\viorng.sys .\Install\Win8\x86
copy .\viorng\objfre_win8_x86\i386\viorng.inf .\Install\Win8\x86
copy .\viorng\objfre_win8_x86\i386\viorng.pdb .\Install\Win8\x86
copy "C:\Program Files (x86)\Windows Kits\8.0\Redist\wdf\x86\WdfCoInstaller01011.dll" .\Install\Win8\x86

call ..\tools\callVisualStudio.bat 11 .\cng\um\viorngum.vcxproj /Rebuild "Release|Win32" /Out .\cng\um\buildfre_win8_x86.log
copy .\cng\um\Win32\Release\viorngum.dll .\Install\Win8\x86
copy .\cng\um\Win32\Release\viorngum.pdb .\Install\Win8\x86

call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x86
inf2cat /driver:.\Install\Win8\x86 /os:8_X86

endlocal
goto :eof

:WIN8_64
setlocal

set _NT_TARGET_VERSION=0x602
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call :create_version_file "viorng\2012-defines.h"
call :create_version_file "cng\um\2012-defines.h"

call ..\tools\callVisualStudio.bat 11 .\viorng\viorng.vcxproj /Rebuild "Win8 Release|x64" /Out .\viorng\buildfre_win8_amd64.log
mkdir .\Install\Win8\amd64
copy .\viorng\objfre_win8_amd64\amd64\viorng.sys .\Install\Win8\amd64
copy .\viorng\objfre_win8_amd64\amd64\viorng.inf .\Install\Win8\amd64
copy .\viorng\objfre_win8_amd64\amd64\viorng.pdb .\Install\Win8\amd64
copy "C:\Program Files (x86)\Windows Kits\8.0\Redist\wdf\x64\WdfCoInstaller01011.dll" .\Install\Win8\amd64

call ..\tools\callVisualStudio.bat 11 .\cng\um\viorngum.vcxproj /Rebuild "Release|x64" /Out .\cng\um\buildfre_win8_amd64.log
copy .\cng\um\x64\Release\viorngum.dll .\Install\Win8\amd64
copy .\cng\um\x64\Release\viorngum.pdb .\Install\Win8\amd64

call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x64
inf2cat /driver:.\Install\Win8\amd64 /os:8_X64,Server8_X64

endlocal
goto :eof

:create_version_file
del  "%~1"
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ% >  "%~1"
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN% >> "%~1"
echo #define _MAJORVERSION_ %_MAJORVERSION_% >> "%~1"
echo #define _MINORVERSION_ %_MINORVERSION_% >> "%~1"
goto :eof
