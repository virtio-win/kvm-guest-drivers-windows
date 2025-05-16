:: %1 (optional) debug level, default = 6
:: meaningful values are 4..12, <4 in fact will work like 4
set level=6
if not "%1"=="" set level=%1
PUSHD "%~dp0"
call collectTrace.bat netkvm "{5666D67E-281E-43ED-8B8D-4347080198AA}" %level%
POPD