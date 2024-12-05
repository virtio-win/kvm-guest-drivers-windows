@echo off
setlocal enabledelayedexpansion

rem  Add Legacy DVL folders here, seperated by a space, in the format
rem  DVLbbbb, where bbbb is the Windows version, e.g. DVL1903.
rem  If you do not wish to build any Legacy DVLs, leave the 
rem  _legacy_dvls_ variable equal to NULL.
set _legacy_dvls_=DVL1903 DVL1607

if "%_legacy_dvls_%"=="" (
  echo INFO : No Legacy DVLs were selected for building.
  goto :eof
)
if "%DVL1607%"=="" set DVL1607=C:\DVL1607
if "%DVL1903%"=="" set DVL1903=C:\DVL1903
set "ProjDir=%~1"
set "IntDir_DVL=%~1%~2"
set "TargetName_DVL=%~3"
set "CONFIGURATION_DVL=%~4"
set "PLATFORM_DVL=%~5"
if exist "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt" (
  del /f "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
)
echo.
call :proc_legacy_dvls %_legacy_dvls_%
endlocal
goto :eof

:proc_legacy_dvls
if "%~1"=="" goto :eof
call :do_dvl %1
shift
goto :proc_legacy_dvls

:do_dvl
set dvl_ver=%1
set dvl_ver=%dvl_ver:~-4%
if exist !%~1! (
  echo Found !%~1!. Building Driver Verification Log for Windows 10 version !dvl_ver!...
  !%~1!\dvl.exe
  if "!errorlevel!" NEQ "0" (
    echo ERROR building Driver Verification Log for Windows 10 version !dvl_ver!.
    echo The DVL file %ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML will NOT exist.
    echo.
    echo !dvl_ver!,fail >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
    goto :eof
  )
  echo Copying DVL to %ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML.
  copy /y "%ProjDir%%TargetName_DVL%.DVL.XML" "%ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML"
  echo !dvl_ver!,!dvl_ver! >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
) else (
  echo The !%~1! directory was not found. Unable to build Driver Verification Log.
  if "%~1"=="DVL1607" (
    if exist "%ProjDir%%TargetName_DVL%.DVL-win10-1903.XML" (
      echo Creating Driver Verification Log for Windows 10 version !dvl_ver! from alternate DVL instead.
      echo Alternate DVL : %TargetName_DVL%.DVL-win10-1903.XML
      findstr /v /c:"General.Checksum" "%ProjDir%%TargetName_DVL%.DVL-win10-1903.XML" > "%ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML"
      echo 1607,1903 >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
    ) else (
      if exist "%ProjDir%%TargetName_DVL%.DVL-win10-latest.XML" (
        echo Creating Driver Verification Log for Windows 10 version !dvl_ver! from alternate DVL instead.
        echo Alternate DVL : %TargetName_DVL%.DVL-win10-latest.XML
        findstr /v /c:"General.Checksum" "%ProjDir%%TargetName_DVL%.DVL-win10-latest.XML" | findstr /v /c:".Semmle." > "%ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML"
        echo 1607,latest >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
      ) else (
        echo Unable to create Driver Verification Log from alternates as no suitable alternate exists.
        echo.
        echo 1607,fail >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
        goto :eof
      )
    )
    echo Driver Verification Log Created. You can locate the Driver Verification Log file at:
    echo %ProjDir%%TargetName_DVL%.DVL-win10-!dvl_ver!.XML
  )
  if "%~1"=="DVL1903" (
        echo Unable to create Driver Verification Log from alternates as no suitable alternate exists.
        echo.
        echo 1903,fail >> "%ProjDir%%TargetName_DVL%.legacy_dvl_result.txt"
        goto :eof
  )
)
echo Finished creating Driver Verification Log for Windows 10 version !dvl_ver!.
echo.
goto :eof
