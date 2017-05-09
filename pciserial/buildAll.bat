setlocal
call clean.bat
rem call ..\Tools\SetVsEnv 14 x64

call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64

mkdir Install\rhel

copy qemupciserial.* .\Install\
inf2cat /driver:Install /os:XP_X86,Server2003_X86,XP_X64,Server2003_X64,Vista_X86,Server2008_X86,Vista_X64,Server2008_X64,7_X86,7_X64,Server2008R2_X64,8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64

copy rhel\qemupciserial.* .\Install\rhel
inf2cat /driver:Install\rhel /os:XP_X86,Server2003_X86,XP_X64,Server2003_X64,Vista_X86,Server2008_X86,Vista_X64,Server2008_X64,7_X86,7_X64,Server2008R2_X64,8_X86,8_X64,Server8_X64,6_3_X86,6_3_X64,Server6_3_X64,10_X86,10_X64,Server10_X64

endlocal
