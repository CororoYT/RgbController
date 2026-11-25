@echo off
title RGBController - Disinstallazione

net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
    exit /b
)

echo.
echo  Disinstallazione di RGBController in corso...
taskkill /F /IM RGBController.exe >nul 2>&1
taskkill /F /IM SensorBridge.exe >nul 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desk = [Environment]::GetFolderPath('Desktop');" ^
    "Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $desk 'RGBController.lnk');" ^
    "$sm = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'RGBController.lnk';" ^
    "Remove-Item -Force -ErrorAction SilentlyContinue $sm"

set "DEST=%ProgramFiles%\RGBController"
if exist "%DEST%" (
    cd /d "%TEMP%"
    rmdir /S /Q "%DEST%"
)

echo  RGBController disinstallato correttamente.
pause
