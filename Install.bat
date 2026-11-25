@echo off
title RGBController - Installer
cd /d "%~dp0"

:: Richiedi privilegi Amministratore per installare in Program Files
net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
    exit /b
)

echo.
echo  ======================================================
echo   RGBController - Installazione Automatica (Windows)
echo  ======================================================
echo.

set "DEST=%ProgramFiles%\RGBController"
set "LHM_DEST=%DEST%\tools\LibreHardwareMonitor"
if not exist "%LHM_DEST%" mkdir "%LHM_DEST%"
if not exist "%DEST%\config" mkdir "%DEST%\config"

:: 1) Copia o scarica RGBController.exe
if exist "%~dp0RGBController.exe" (
    echo  [1/4] Copia RGBController.exe...
    copy /Y "%~dp0RGBController.exe" "%DEST%\RGBController.exe" >nul
) else (
    echo  [1/4] Download RGBController.exe dall'ultima Release GitHub...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference='SilentlyContinue';" ^
        "$zip = Join-Path $env:TEMP 'RGBController-Portable.zip';" ^
        "Invoke-WebRequest -Uri 'https://github.com/CororoYT/RgbController/releases/latest/download/RGBController-Portable.zip' -OutFile $zip -UseBasicParsing;" ^
        "Expand-Archive -Path $zip -DestinationPath '%DEST%' -Force;" ^
        "Remove-Item $zip -Force"
    if errorlevel 1 (
        echo  [ERRORE] Download fallito. Verifica la connessione Internet o compila con build.bat.
        pause
        exit /b 1
    )
)

:: 2) Copia o scarica librerie sensori & compila SensorBridge.exe se mancante
if exist "%~dp0tools\LibreHardwareMonitor\SensorBridge.exe" (
    echo  [2/4] Copia SensorBridge e librerie...
    xcopy /Y /Q "%~dp0tools\LibreHardwareMonitor\*" "%LHM_DEST%\" >nul
) else if not exist "%LHM_DEST%\SensorBridge.exe" (
    echo  [2/4] Download LibreHardwareMonitorLib e compilazione SensorBridge...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference='SilentlyContinue';" ^
        "$zip = Join-Path $env:TEMP 'LHM.zip';" ^
        "Invoke-WebRequest -Uri 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip' -OutFile $zip -UseBasicParsing;" ^
        "Expand-Archive -Path $zip -DestinationPath '%LHM_DEST%' -Force;" ^
        "Remove-Item $zip -Force"
    set "CSC=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
    if exist "%~dp0src\SensorBridge.cs" if exist "%CSC%" (
        "%CSC%" /nologo /optimize /platform:x64 ^
            /reference:"%LHM_DEST%\LibreHardwareMonitorLib.dll" ^
            /reference:"%LHM_DEST%\RAMSPDToolkit-NDD.dll" ^
            /out:"%LHM_DEST%\SensorBridge.exe" "%~dp0src\SensorBridge.cs"
    )
)

:: 3) Copia Uninstall.bat
if exist "%~dp0Uninstall.bat" copy /Y "%~dp0Uninstall.bat" "%DEST%\Uninstall.bat" >nul

:: 4) Crea collegamenti sul Desktop e nel Menu Start
echo  [3/4] Creazione collegamenti Desktop e Menu Start...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ws = New-Object -ComObject WScript.Shell;" ^
    "$desk = [Environment]::GetFolderPath('Desktop');" ^
    "$s1 = $ws.CreateShortcut((Join-Path $desk 'RGBController.lnk'));" ^
    "$s1.TargetPath = '%DEST%\RGBController.exe';" ^
    "$s1.WorkingDirectory = '%DEST%';" ^
    "$s1.Save();" ^
    "$sm = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'RGBController.lnk';" ^
    "$s2 = $ws.CreateShortcut($sm);" ^
    "$s2.TargetPath = '%DEST%\RGBController.exe';" ^
    "$s2.WorkingDirectory = '%DEST%';" ^
    "$s2.Save()"

echo  [4/4] Installazione completata in: %DEST%
echo.
set /p RUNNOW="Vuoi avviare RGBController adesso? (S/N): "
if /I "%RUNNOW%"=="S" start "" "%DEST%\RGBController.exe"
exit /b 0
