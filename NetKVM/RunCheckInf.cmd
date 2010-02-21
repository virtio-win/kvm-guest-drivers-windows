mkdir htm
del /q htm\*
call c:\winddk\6001\tools\chkinf\chkinf.bat Install\2K\x86\netkvm2k.inf Install\Vista\amd64\netkvm.inf Install\Vista\x86\netkvm.inf Install\XP\amd64\netkvm.inf Install\XP\x86\netkvm.inf
start htm\summary.htm


