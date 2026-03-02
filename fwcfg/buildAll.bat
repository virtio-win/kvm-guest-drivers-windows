setlocal
call clean.bat
call ..\build\SetVsEnv x86

inf2cat /uselocaltime /driver:. /os:6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64,Server10_ARM64

mkdir Install
copy qemufwcfg.* .\Install\

endlocal
