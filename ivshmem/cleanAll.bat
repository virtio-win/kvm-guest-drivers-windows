@echo off
set _cln_tgt_=ivshmem
set _cln_subdirs_=test
call ..\build\clean.bat
call :subdir %_cln_subdirs_%
goto :eof

:subdir
if "%~1"=="" goto :eof
call :do_clean %1
shift
goto :subdir

:do_clean
echo Cleaning %_cln_tgt_% ^| %~1 ...
if not exist "%~dp0%~1" (
  echo The %_cln_tgt_%^\%~1 directory was not available for cleaning.
  goto :eof
)
pushd "%~dp0%~1"
if exist ".\cleanall.bat" (
  call cleanall.bat
) else (
  if exist ".\clean.bat" (
    call clean.bat
  ) else (
    if exist "..\..\build\clean.bat" (
      call ..\..\build\clean.bat
    ) else (
      if exist "..\..\..\build\clean.bat" (
        call ..\..\..\build\clean.bat
      ) else (
        echo A cleaner for the %_cln_tgt_%^\%~1 directory could not be found.
      )
    )
  )
)
popd
goto :eof
