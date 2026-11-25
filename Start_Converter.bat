@echo off
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 goto usepython
py -3 converter_gui.py
goto finish
:usepython
python converter_gui.py
:finish
if errorlevel 1 pause
