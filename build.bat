@echo off
cd /d "%~dp0"
set "GCC=%LOCALAPPDATA%\Programs\mingw64\bin\gcc.exe"
if not exist "%GCC%" (
    echo [ERRORE] GCC non trovato in %GCC%
    exit /b 1
)
"%GCC%" -O3 -march=native -std=c11 -Wall -Wextra -mwindows ^
    src\main.c src\hid_drivers.c src\sensor_ipc.c src\settings.c ^
    -o RGBController.exe ^
    -luser32 -lgdi32 -lshell32 -lsetupapi -lhid -ldwmapi -ladvapi32 -lm
if errorlevel 1 exit /b 1
echo Compilazione RGBController.exe completata!
