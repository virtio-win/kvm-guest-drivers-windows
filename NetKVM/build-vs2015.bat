mkdir Install

:: Build Windows Vista
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x86" /Out buildfre_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x64" /Out buildfre_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 7
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x86" /Out buildfre_win7_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x64" /Out buildfre_win7_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x86" /Out buildfre_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x64" /Out buildfre_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 8.1
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x86" /Out buildfre_win8_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x64" /Out buildfre_win8_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: Build Windows 10
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x86" /Out buildfre_win10_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x64" /Out buildfre_win10_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:: XP
pushd NDIS5
call buildall.bat XP
popd
if %ERRORLEVEL% NEQ 0 goto :eof
xcopy /S /I /Y NDIS5\Install\XP\x86 Install\XP\x86

:: XP64
pushd NDIS5
call buildall.bat XP64
popd
if %ERRORLEVEL% NEQ 0 goto :eof
xcopy /S /I /Y NDIS5\Install\XP\amd64 Install\XP\amd64

mkdir Install
mkdir Install\Vista
mkdir Install\Vista\x86
mkdir Install\Vista\amd64
mkdir Install\Win7
mkdir Install\Win7\x86
mkdir Install\Win7\amd64
mkdir Install\Win8
mkdir Install\Win8\x86
mkdir Install\Win8\amd64
mkdir Install\Win8.1
mkdir Install\Win8.1\x86
mkdir Install\Win8.1\amd64
mkdir Install\Win10
mkdir Install\Win10\x86
mkdir Install\Win10\amd64

:: Copy Windows Vista binaries
copy /y x86\VistaRelease\netkvm.pdb Install\Vista\x86\
copy /y x86\VistaRelease\NetKVM-VS2015\netkvm.inf Install\Vista\x86\
copy /y x86\VistaRelease\NetKVM-VS2015\netkvm.cat Install\Vista\x86\
copy /y x86\VistaRelease\NetKVM-VS2015\netkvm.sys Install\Vista\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Vista\x86\
copy /y CoInstaller\VistaRelease\x86\netkvmco.dll Install\Vista\x86\
copy /y CoInstaller\readme.doc Install\Vista\x86\

copy /y x64\VistaRelease\netkvm.pdb Install\Vista\amd64\
copy /y x64\VistaRelease\NetKVM-VS2015\netkvm.inf Install\Vista\amd64\
copy /y x64\VistaRelease\NetKVM-VS2015\netkvm.cat Install\Vista\amd64\
copy /y x64\VistaRelease\NetKVM-VS2015\netkvm.sys Install\Vista\amd64\
copy /y tools\NetKVMTemporaryCert.cer Install\Vista\amd64\
copy /y CoInstaller\VistaRelease\x64\netkvmco.dll Install\Vista\amd64\
copy /y CoInstaller\readme.doc Install\Vista\amd64\

:: Copy Windows 7 binaries
copy /y x86\Win7Release\netkvm.pdb Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.inf Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.cat Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.sys Install\Win7\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win7\x86\
copy /y CoInstaller\Win7Release\x86\netkvmco.dll Install\Win7\x86\
copy /y CoInstaller\readme.doc Install\Win7\x86\

copy /y x64\Win7Release\netkvm.pdb Install\Win7\amd64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.inf Install\Win7\amd64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.cat Install\Win7\amd64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.sys Install\Win7\amd64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win7\amd64\
copy /y CoInstaller\Win7Release\x64\netkvmco.dll Install\Win7\amd64\
copy /y CoInstaller\readme.doc Install\Win7\amd64\

:: Copy Windows 8 binaries
copy /y x86\Win8Release\netkvm.pdb Install\Win8\x86\
copy /y x86\Win8Release\NetKVM-VS2015\netkvm.inf Install\Win8\x86\
copy /y x86\Win8Release\NetKVM-VS2015\netkvm.cat Install\Win8\x86\
copy /y x86\Win8Release\NetKVM-VS2015\netkvm.sys Install\Win8\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win8\x86\
copy /y CoInstaller\Win8Release\x86\netkvmco.dll Install\Win8\x86\
copy /y CoInstaller\readme.doc Install\Win8\x86\

copy /y x64\Win8Release\netkvm.pdb Install\Win8\amd64\
copy /y x64\Win8Release\NetKVM-VS2015\netkvm.inf Install\Win8\amd64\
copy /y x64\Win8Release\NetKVM-VS2015\netkvm.cat Install\Win8\amd64\
copy /y x64\Win8Release\NetKVM-VS2015\netkvm.sys Install\Win8\amd64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win8\amd64\
copy /y CoInstaller\Win8Release\x64\netkvmco.dll Install\Win8\amd64\
copy /y CoInstaller\readme.doc Install\Win8\amd64\

:: Copy Windows 8.1 binaries
copy /y x86\Win8.1Release\netkvm.pdb Install\Win8.1\x86\
copy /y x86\Win8.1Release\NetKVM-VS2015\netkvm.inf Install\Win8.1\x86\
copy /y x86\Win8.1Release\NetKVM-VS2015\netkvm.cat Install\Win8.1\x86\
copy /y x86\Win8.1Release\NetKVM-VS2015\netkvm.sys Install\Win8.1\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win8.1\x86\
copy /y CoInstaller\Win8.1Release\x86\netkvmco.dll Install\Win8.1\x86\
copy /y CoInstaller\readme.doc Install\Win8.1\x86\

copy /y x64\Win8.1Release\netkvm.pdb Install\Win8.1\amd64\
copy /y x64\Win8.1Release\NetKVM-VS2015\netkvm.inf Install\Win8.1\amd64\
copy /y x64\Win8.1Release\NetKVM-VS2015\netkvm.cat Install\Win8.1\amd64\
copy /y x64\Win8.1Release\NetKVM-VS2015\netkvm.sys Install\Win8.1\amd64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win8.1\amd64\
copy /y CoInstaller\Win8.1Release\x64\netkvmco.dll Install\Win8.1\amd64\
copy /y CoInstaller\readme.doc Install\Win8.1\amd64\

:: Copy Windows 10 binaries
copy /y x86\Win10Release\netkvm.pdb Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.inf Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.cat Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.sys Install\Win10\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\x86\
copy /y CoInstaller\Win10Release\x86\netkvmco.dll Install\Win10\x86\
copy /y CoInstaller\readme.doc Install\Win10\x86\

copy /y x64\Win10Release\netkvm.pdb Install\Win10\amd64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.inf Install\Win10\amd64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.cat Install\Win10\amd64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.sys Install\Win10\amd64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\amd64\
copy /y CoInstaller\Win10Release\x64\netkvmco.dll Install\Win10\amd64\
copy /y CoInstaller\readme.doc Install\Win10\amd64\

call create_dvl_log_vs2015.cmd
