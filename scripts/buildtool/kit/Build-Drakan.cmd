@echo off
setlocal
cd /d "%~dp0"
rem Drakan PC builder: checks the tools, asks for your ISO and builds the game into dist\DrakanPC.
set "PWSH=%~dp0tools\pwsh\pwsh.exe"
if exist "%PWSH%" goto run
set "PWSH=pwsh"
where pwsh >nul 2>&1
if errorlevel 1 (
    echo PowerShell 7 is required. Install it with:  winget install Microsoft.PowerShell
    echo or from https://aka.ms/powershell, then run Build-Drakan.cmd again.
    pause
    exit /b 1
)
:run
title Building Drakan: The Ancients' Gates
"%PWSH%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\buildtool\Build-Drakan.ps1" %*
set RESULT=%ERRORLEVEL%
echo.
if not "%RESULT%"=="0" (
    echo The build stopped with an error ^(exit code %RESULT%^). See the messages above.
    echo The log is in "%~dp0build-log.txt", the compiler output in "%~dp0build\game-output.log".
)
pause
exit /b %RESULT%
