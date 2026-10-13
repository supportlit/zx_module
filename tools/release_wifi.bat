@echo off
setlocal
set SCRIPT_DIR=%~dp0
set PYTHON_EXE=%SCRIPT_DIR%..\prebuilts\win32\python3\python3.exe

if exist "%PYTHON_EXE%" (
    "%PYTHON_EXE%" "%SCRIPT_DIR%release_wifi.py" %*
) else (
    python "%SCRIPT_DIR%release_wifi.py" %*
)
endlocal
