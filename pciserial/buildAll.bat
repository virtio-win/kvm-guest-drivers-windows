setlocal
call clean.bat
call ..\Tools\SetVsEnv x86

mkdir Install\rhel

copy qemupciserial.* .\Install\
inf2cat /driver:Install /uselocaltime /os:8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64

copy rhel\qemupciserial.* .\Install\rhel
inf2cat /driver:Install\rhel /uselocaltime /os:8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64

endlocal
