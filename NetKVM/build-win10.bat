:: Build Windows 10
setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-win10.sln /Rebuild "Release|x86" /Out buildfre_win10_x86.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

setlocal
call tools\set_version.bat
call ..\tools\callVisualStudio.bat 14 netkvm-win10.sln /Rebuild "Release|x64" /Out buildfre_win10_amd64.log
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof


mkdir Install
mkdir Install\Win10
mkdir Install\Win10\x86
mkdir Install\Win10\x64

copy /y x86\Release\netkvm.pdb Install\Win10\x86\
copy /y x86\Release\NetKVM-Win10\netkvm.inf Install\Win10\x86\
copy /y x86\Release\NetKVM-Win10\netkvm.cat Install\Win10\x86\
copy /y x86\Release\NetKVM-Win10\netkvm.sys Install\Win10\x86\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\x86\

copy /y x64\Release\netkvm.pdb Install\Win10\x64\
copy /y x64\Release\NetKVM-Win10\netkvm.inf Install\Win10\x64\
copy /y x64\Release\NetKVM-Win10\netkvm.cat Install\Win10\x64\
copy /y x64\Release\NetKVM-Win10\netkvm.sys Install\Win10\x64\
copy /y tools\NetKVMTemporaryCert.cer Install\Win10\x64\