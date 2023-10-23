setlocal
call clean.bat
call ..\Tools\SetVsEnv x86

mkdir Install

copy smbus\smbus.* .\Install\
inf2cat /driver:Install /uselocaltime /os:6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64

endlocal
