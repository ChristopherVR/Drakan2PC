@echo off
setlocal
cd /d "%~dp0"
rem Use drakan.ini, including the saved World frame rate selection.
start "" "%~dp0drakan.exe"
endlocal
