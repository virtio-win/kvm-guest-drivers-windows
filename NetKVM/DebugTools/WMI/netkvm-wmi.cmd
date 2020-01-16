:: Example of accessing NetKvm internals via WMI commands:
@echo off
if "%1"=="" goto help
if /i "%1"=="debug" goto debug
if /i "%1"=="stat" goto stat
if /i "%1"=="reset" goto reset
if /i "%1"=="rss" goto rss_set
goto help
:debug
call :dowmic netkvm_logging set level=%2
goto :eof

:stat
call :dowmic netkvm_statistics get
goto :eof

:reset
call :dowmic netkvm_statistics set rxChecksumOK=0
goto :eof

:rss
call :dowmic NetKvm_RssDiagnostics get
goto :eof

:rss_set
if "%2"=="" goto rss
call :dowmic NetKvm_RssDiagnostics set DeviceSupport=%2
goto :eof

:dowmic
wmic /namespace:\\root\wmi path %*
goto :eof

:help
echo Example of WMI controls to NetKvm
echo %~nx0 command parameter
echo debug level            Controls debug level (use level 0..5)
echo stat                   Retrieves internal statistics
echo reset                  Resets internal statistics
echo rss                    Query RSS statistics
echo rss 0/1                Disable/enable RSS device support
goto :eof

