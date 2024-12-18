@echo off
if "%~1"=="-quiet" (set loglevel=0)
if "%~1"=="-debug" (set loglevel=2)
if "%~1"=="" (set loglevel=1)
set _cln_tgt_=Repo Root
set _cln_subdirs_=NetKVM viostor vioscsi VirtIO Balloon vioserial viorng vioinput viofs pvpanic viosock viogpu viomem viocrypt ivshmem pciserial Q35 fwcfg fwcfg64
echo Cleaning %_cln_tgt_% ...
call .\build\clean.bat %*
echo.
call :subdir %_cln_subdirs_%
echo CLEANING COMPLETE.
goto :eof

:subdir
if "%~1"=="" goto :eof
call :do_clean %1
shift
goto :subdir

:do_clean
if not exist "%~dp0%~1" (
  echo The %~1 directory was not available for cleaning.
  echo.
  goto :eof
)
pushd "%~dp0%~1"
if exist ".\cleanall.bat" (
  echo Cleaning %~1 ...
  call cleanall.bat
) else (
  if exist ".\clean.bat" (
    echo Cleaning %~1 ...
    call clean.bat
  ) else (
    if exist "..\..\build\clean.bat" (
      echo Cleaning %~1 ...
      call ..\..\build\clean.bat
    ) else (
      if exist "..\..\..\build\clean.bat" (
        echo Cleaning %~1 ...
        call ..\..\..\build\clean.bat
      ) else (
        echo A cleaner for the %~1 directory could not be found.
      )
    )
  )
)
echo.
popd
goto :eof
