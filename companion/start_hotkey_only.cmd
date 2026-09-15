@echo off
setlocal
cd /d "%~dp0"

echo AI Passport hotkey forwarder starting...
echo UP = Right Shift, DOWN = Enter, microphone = computer default input.

set "LOCAL_PYTHON=%LocalAppData%\Programs\Python\Python311\python.exe"
if exist "%LOCAL_PYTHON%" (
  "%LOCAL_PYTHON%" "%~dp0hotkey_forwarder.py" --no-audio --key right-shift --mode tap
) else (
  py -3 "%~dp0hotkey_forwarder.py" --no-audio --key right-shift --mode tap
)

if errorlevel 1 pause
