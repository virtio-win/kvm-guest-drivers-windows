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
if /i "%1"=="cx" goto cx
if /i "%1"=="mig" goto mig

goto help
:debug
call :dowmic_set netkvm_logging level %2
goto :eof

:cfg
call :dowmic_get1 netkvm_config
goto :eof

:stat
echo ---- TX statistics ---
call :diag tx
echo ---- RX statistics ---
call :diag rx
echo ---- RSS statistics --
call :diag rss
echo ---- CX statistics --
call :diag ctrl
echo ---- Migration statistics --
call :diag mig
goto :eof

:tx
call :diag tx
goto :eof

:rx
call :diag rx
goto :eof

:cx
call :diag ctrl
goto :eof

:mig
call :diag mig
goto :eof

:reset
set resettype=7
if "%2"=="rx" set resettype=1
if "%2"=="tx" set resettype=2
if "%2"=="rss" set resettype=4
echo resetting type %resettype%...
call :dowmic_set netkvm_diagreset type %resettype%
goto :eof

:rss
call :diag rss
goto :eof

:rss_set
if "%2"=="" goto rss
call :dowmic_set NetKvm_DeviceRss value %2
goto :eof

:diag
call :dowmic_get2 netkvm_diag %1
goto :eof

:: %1 - class
:: %2 - field
:dowmic_get2
powershell "get-ciminstance -namespace root\wmi -classname %1 | select-object -expandproperty %2 | fl" | findstr /v /c:__ /c:PSComputerName /c:Active | findstr /v /r ^^^$
goto :eof

:: %1 - class
:dowmic_get1
powershell "get-ciminstance -namespace root\wmi -classname %1 | fl" | findstr /v /c:__ /c:PSComputerName /c:Active | findstr /v /r ^^^$
goto :eof

:: %1 - class
:: %2 - field name
:: %3 - value
:dowmic_set
powershell "get-ciminstance -namespace root\wmi -classname %1 | set-ciminstance -arguments @{%2=%3}"
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
echo cx                     Retrieves internal statistics for controls
echo mig                    Retrieves live-migration (guest-announce) info
echo rss 0/1                Disable/enable RSS device support
echo reset [tx^|rs^|rss]      Resets internal statistics(default=all)
goto :eof

