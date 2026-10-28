@echo off
cd /d "%~dp0"
where py >nul 2>&1
if errorlevel 1 (
    python serve_web.py --open %*
) else (
    py -3 serve_web.py --open %*
)
