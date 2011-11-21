@echo off
call :set_sdk_path
call :setshortpath PSDK_INC_PATH %__sdk_path__%\Include
call :setshortpath PSDK_LIB_PATH %__sdk_path__%\Lib
call :clean_sdk_path
goto :eof

:setshortpath
set %1=%~s2
goto :eof

:set_sdk_path
set __sdk_path__="C:\Program Files\Microsoft SDKs\Windows\v6.0A"
if NOT "%SDK_PATH%"=="" set __sdk_path__="%SDK_PATH%"
goto :eof

:clean_sdk_path
set __sdk_path__=
goto :eof
