@echo off
setlocal
rem Check for x86 viosock libraries and build them if needed...
call :do_viosock %*
if ERRORLEVEL 1 (
  endlocal
  exit /B 1
)
endlocal
goto :eof

:do_viosock
rem Lay up some variables
set BUILD_FILE=%~1
set BUILD_ARCH=%~2
set BUILD_DIR=%~3
set TARGET=%~4
set BUILD_FLAVOUR=%~5
set VIOSOCK_PREBUILD_X86_LIBS=
set BUILD_FAILED=

rem Which solutions need this?
if "%BUILD_FILE%"=="virtio-win.sln" (
  set VIOSOCK_PREBUILD_X86_LIBS=1
)
if "%BUILD_FILE%"=="viosock.sln" (
  set VIOSOCK_PREBUILD_X86_LIBS=1
)

if "%VIOSOCK_PREBUILD_X86_LIBS%" EQU "1" (
  rem Only proceed if we are building for x64
  if %BUILD_ARCH%==x64 (
    rem Check for x86 viosock libraries and build them if needed...
    if not exist "%BUILD_DIR%viosock\lib\x86\%TARGET%%BUILD_FLAVOR%\viosocklib.dll" (
      echo.
      call :log_warn "ATTENTION : Need to build x86 viosock libraries before building for amd64..."
      setlocal
      set VIRTIO_WIN_NO_ARM=1
      if "%BUILD_FILE%"=="virtio-win.sln" (
        pushd "%BUILD_DIR%viosock\lib"
      )
      if "%BUILD_FILE%"=="viosock.sln" (
        pushd "%BUILD_DIR%lib"
      )
      call ..\..\build\build.bat viosocklib.vcxproj %TARGET% x86
      if ERRORLEVEL 1 (
        set BUILD_FAILED=1
      )
      popd
      if "%BUILD_FAILED%" EQU "1" (
        exit /B 1
      )
      call :log_success "Successfully built the x86 viosock libraries."
      call :log_info "Continuing with amd64 build..."
      echo.
      endlocal
    )
  )
)
goto :eof

:log_info
call :clr_print %_c_Cyn% "%~1"
goto :eof

:log_warn
call :clr_print %_c_Yel% "%~1"
goto :eof

:log_error
call :clr_print %_c_Red% "%~1"
goto :eof

:log_success
call :clr_print %_c_Grn% "%~1"
goto :eof

:clr_print
@echo %z_esc%[%~1%~2%z_esc%[%~3%~4%z_esc%[%~5%~6%z_esc%[%~7%~8%z_esc%[0m
goto :eof
