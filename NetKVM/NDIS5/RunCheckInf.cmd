mkdir htm
del /q htm\*
call c:\winddk\7600.16385.1\tools\chkinf\chkinf.bat Install\Vista\amd64\netkvm.inf Install\Vista\x86\netkvm.inf Install\XP\amd64\netkvm.inf Install\XP\x86\netkvm.inf
start htm\summary.htm


