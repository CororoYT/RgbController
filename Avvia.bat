@echo off
:: ============================================================
::  RGBController – Launcher con elevazione UAC automatica
::  Avvia LibreHardwareMonitor + OpenRGB + App in sequenza
:: ============================================================
title RGBController

:: Controlla se siamo già admin
net session >nul 2>&1
if %errorLevel% == 0 (
    goto :run_as_admin
) else (
    goto :elevate
)

:elevate
:: Rilancia se stesso con UAC (richiede conferma una volta sola)
echo Richiesta elevazione amministratore per il controllo ventole...
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
exit /b

:run_as_admin
cd /d "%~dp0"
echo.
echo  ╔══════════════════════════════════════════╗
echo  ║   RGBController – Avvio automatico       ║
echo  ╚══════════════════════════════════════════╝
echo.
echo  Avvio componenti in corso...
python launcher.py
if errorlevel 1 (
    echo.
    echo [ERRORE] Il launcher ha restituito un errore.
    pause
)
