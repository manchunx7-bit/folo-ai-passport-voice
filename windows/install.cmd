@echo off
setlocal
cd /d "%~dp0"
title AI Passport Voice - Install Python dependencies
py -3.11 --version >nul 2>&1
if errorlevel 1 (
  echo Install Python 3.11 x64 from https://www.python.org/downloads/windows/
  echo Include the Python launcher, then run this file again.
  pause
  exit /b 1
)
py -3.11 -m venv .venv
if errorlevel 1 goto fail
".venv\Scripts\python.exe" -m pip install -r requirements-hotkey.txt
if errorlevel 1 goto fail
echo Installation complete. Install VB-CABLE separately and reboot.
pause
exit /b 0
:fail
echo Installation failed. Read the error above; see docs for troubleshooting.
pause
exit /b 1
