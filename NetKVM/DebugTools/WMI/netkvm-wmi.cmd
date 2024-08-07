:: Example of accessing NetKvm internals via WMI commands:
@echo off
if "%1"=="" goto help
if /i "%1"=="debug" goto debug
if /i "%1"=="cfg" goto cfg
if /i "%1"=="stat" goto stat
if /i "%1"=="reset" goto reset
if /i "%1"=="rss" goto rss_set
if /i "%1"=="tx" goto tx
if /i "%1"=="rx" goto rx

goto help
:debug
call :dowmic netkvm_logging set level=%2
goto :eof

:cfg
call :dowmic netkvm_config get /value
goto :eof

:stat
echo ---- TX statistics ---
call :diag tx
echo ---- RX statistics ---
call :diag rx
echo ---- RSS statistics --
call :diag rss
goto :eof

:tx
call :diag tx
goto :eof

:rx
call :diag rx
goto :eof

:reset
set resettype=7
if "%2"=="rx" set resettype=1
if "%2"=="tx" set resettype=2
if "%2"=="rss" set resettype=4
echo resetting type %resettype%...
call :dowmic netkvm_diagreset set type=%resettype%
goto :eof

:rss
call :diag rss
goto :eof

:rss_set
if "%2"=="" goto rss
call :dowmic NetKvm_DeviceRss set value=%2
goto :eof

:diag
call :dowmic netkvm_diag get %1 /value
goto :eof

:dowmic
::echo executing %*
wmic /namespace:\\root\wmi path %* | findstr /v __ | findstr /v /r ^^^$
goto :eof

:help
echo Example of WMI controls to NetKvm
echo %~nx0 command parameter
echo debug level            Controls debug level (use level 0..5)
echo cfg                    Retrieves current configuration
echo stat                   Retrieves all internal statistics
echo tx                     Retrieves internal statistics for transmit
echo rx                     Retrieves internal statistics for receive
echo rss                    Retrieves internal statistics for RSS
echo rss 0/1                Disable/enable RSS device support
echo reset [tx^|rs^|rss]      Resets internal statistics(default=all)
goto :eof

