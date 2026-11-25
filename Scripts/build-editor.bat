@echo off
rem Builds Gorgon editor targets with the MSVC environment from VsDevCmd.
rem Usage: scripts\build-editor.bat <cmake target> [more targets...]
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo >nul
if errorlevel 1 (
    echo FAIL: VsDevCmd did not initialise
    exit /b 1
)
cd /d "%~dp0.."
cmake --build build\WindowsTesting --config Release --target %*
exit /b %errorlevel%
