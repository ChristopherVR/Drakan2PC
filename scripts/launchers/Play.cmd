@echo off
setlocal
rem Normal launch: drakan.ini decides the world frame rate (see Play-Original30.cmd for the original 30 FPS).
pushd "%~dp0"
start "Drakan" drakan.exe %*
popd
endlocal
