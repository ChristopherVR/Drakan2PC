@echo off
setlocal
cd /d "%~dp0"
rem Restore the original world cadence for this launch; keep saved settings intact.
set DRAKAN_WORLD_RATE=30
set DRAKAN_FRAME_LIMIT=30
set DRAKAN_VSYNC=0
start "" "%~dp0drakan.exe"
endlocal
