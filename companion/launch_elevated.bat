@echo off
setlocal
cd /d "%~dp0"
set "LOCAL_PYTHON=%LocalAppData%\Programs\Python\Python311\python.exe"

if exist "%LOCAL_PYTHON%" (
  "%LOCAL_PYTHON%" -u "%~dp0hotkey_forwarder.py" --no-audio --key right-shift --mode tap > "%~dp0hotkey_forwarder.log" 2>&1
) else (
  py -3 -u "%~dp0hotkey_forwarder.py" --no-audio --key right-shift --mode tap > "%~dp0hotkey_forwarder.log" 2>&1
)
