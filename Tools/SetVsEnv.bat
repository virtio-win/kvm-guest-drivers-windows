@echo off
set __VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017

if not "%EnterpriseWDK%"=="" goto ewdk_ready
if "%EWDK_DIR%"=="" goto vs_vars
call %EWDK_DIR%\BuildEnv\SetupBuildEnv.cmd
call :add_path "%VCToolsRedistDir%onecore\x86\Microsoft.VC142.OPENMP"
goto :end

:vs_vars
if not "%VSFLAVOR%"=="" goto :knownVS
call :checkvs
echo USING %VSFLAVOR% Visual Studio

:knownVS
echo %0: Setting NATIVE ENV for %1 (VS %VSFLAVOR%)...
call "%__VS_PATH%\%VSFLAVOR%\VC\Auxiliary\Build\vcvarsall.bat" %1
goto :end

:checkvs
set VSFLAVOR=Professional
if exist "%__VS_PATH%\Community\VC\Auxiliary\Build\vcvarsall.bat" set VSFLAVOR=Community
goto :eof

:ewdk_ready
echo We are already in EWDK version: %Version_Number%
goto :ewdk_end

:add_path
echo %path% | findstr /i /c:"%~1"
if not errorlevel 1 goto :eof
echo Adding path %~1
set path=%path%;%~1
goto :eof

:add_def
echo %PreprocessorDefinitions% | findstr /i /c:"%~1"
if not errorlevel 1 goto :eof
echo Adding PreprocessorDefinitions%~1
if not "%PreprocessorDefinitions%"=="" set PreprocessorDefinitions=%PreprocessorDefinitions%;
set PreprocessorDefinitions=%PreprocessorDefinitions%%~1
goto :eof

:add_include
echo %AdditionalIncludeDirectories% | findstr /i /c:"%~1"
if not errorlevel 1 goto :eof
echo Adding AdditionalIncludeDirectories %~1
if not "%AdditionalIncludeDirectories%"=="" set AdditionalIncludeDirectories=%AdditionalIncludeDirectories%;
set AdditionalIncludeDirectories=%AdditionalIncludeDirectories%%~1
goto :eof

:add_lib
echo %AdditionalLibraryDirectories% | findstr /i /c:"%~1"
if not errorlevel 1 goto :eof
echo Adding AdditionalLibraryDirectories %~1
if not "%AdditionalLibraryDirectories%"=="" set AdditionalLibraryDirectories=%AdditionalLibraryDirectories%;
set AdditionalLibraryDirectories=%AdditionalLibraryDirectories%%~1
goto :eof

:add_lib_x64
if "%BUILD_ARCH%"=="x64" call :add_lib %1
goto :eof

:add_lib_x86
if "%BUILD_ARCH%"=="x86" call :add_lib %1
goto :eof

:add_lib_ARM64
if "%BUILD_ARCH%"=="ARM64" call :add_lib %1
goto :eof

:end
