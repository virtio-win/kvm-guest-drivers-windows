@echo off

rem ================================================================================
rem Build script for all project cabinet (.cab) files
rem
rem This script iterates through a predefined list of .ddf files and uses
rem makecab.exe to generate the corresponding .cab archives.
rem
rem The script will stop if any of the makecab operations fail.
rem ================================================================================

rem A list of all .ddf files to be built
set DDF_FILES=win10_arm64.ddf win10_x64.ddf win10_x86.ddf win11_arm64.ddf win11_x64.ddf

echo.
echo Starting cabinet file build process...
echo.

for %%F in (%DDF_FILES%) do (
    echo Generating cabinet from %%F
    makecab /f "%%F"
    if errorlevel 1 (
        echo ERROR: Build failed for %%F. Stopping.
        exit /b 1
    )
)

echo.
echo All cabinet files built successfully.
