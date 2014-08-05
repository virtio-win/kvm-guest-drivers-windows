setlocal
call clean.bat
call ..\Tools\SetVsEnv 12 x64

inf2cat /driver:. /os:XP_X86,Server2003_X86,XP_X64,Server2003_X64,Vista_X86,Server2008_X86,Vista_X64,Server2008_X64,7_X86,7_X64,Server2008R2_X64,8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64

mkdir Install
copy qemupciserial.* .\Install\

endlocal