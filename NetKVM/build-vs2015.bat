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


mkdir Install
mkdir Install\Win7
mkdir Install\Win7\x86
mkdir Install\Win7\x64
mkdir Install\Win10
mkdir Install\Win10\x86
mkdir Install\Win10\x64

:: Copy Windows 10 binaries
copy /y x86\Win10Release\netkvm.pdb Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.inf Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.cat Install\Win10\x86\
copy /y x86\Win10Release\NetKVM-VS2015\netkvm.sys Install\Win10\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\x86\

copy /y x64\Win10Release\netkvm.pdb Install\Win10\x64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.inf Install\Win10\x64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.cat Install\Win10\x64\
copy /y x64\Win10Release\NetKVM-VS2015\netkvm.sys Install\Win10\x64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\x64\

:: Copy Windows 7 binaries
copy /y x86\Win7Release\netkvm.pdb Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.inf Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.cat Install\Win7\x86\
copy /y x86\Win7Release\NetKVM-VS2015\netkvm.sys Install\Win7\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win7\x86\

copy /y x64\Win7Release\netkvm.pdb Install\Win7\x64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.inf Install\Win7\x64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.cat Install\Win7\x64\
copy /y x64\Win7Release\NetKVM-VS2015\netkvm.sys Install\Win7\x64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win7\x64\