::set PSDK_INC_PATH=C:\PROGRA~1\MI2578~1\Windows\v6.0A\Include
::set PSDK_LIB_PATH=C:\PROGRA~1\MI2578~1\Windows\v6.0A\Lib
@echo off
set __sdk_path__="C:\Program Files\Microsoft SDKs\Windows\v6.0A"
call :setshortpath PSDK_INC_PATH %__sdk_path__%\Include
call :setshortpath PSDK_LIB_PATH %__sdk_path__%\Lib
goto :eof

:setshortpath
set %1=%~s2
goto :eof
