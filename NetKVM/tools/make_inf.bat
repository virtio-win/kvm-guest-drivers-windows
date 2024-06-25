: Param1 - DirectoryOfINF 
: Param2 - DDKSpec

goto makeinf

:prepareinf
::original inf %1, copy to %2, define OS %3
echo processing %1 for %3
cl /nologo -DINCLUDE_TEST_PARAMS -D%3 /I. /EP %1 > %~nx1
echo cleaning INF file...
cscript /nologo .\tools\cleanemptystrings.vbs %~nx1 > %2\%~nx1
del %~nx1
goto :eof

:makeinf
echo calling prepareinf
call :prepareinf %1NetKVM.inf %1 %2
