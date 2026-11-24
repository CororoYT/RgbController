@echo off
title RGBController

net session >nul 2>&1
if %errorLevel% == 0 (
    goto :run_as_admin
) else (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
    exit /b
)

:run_as_admin
cd /d "%~dp0"
if exist "RGBController.exe" (
    start "" "RGBController.exe"
    exit /b
)
python launcher.py
if errorlevel 1 pause
