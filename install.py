"""
install.py – Setup iniziale automatico di RGBController.
Eseguire una sola volta dopo aver scaricato il progetto da GitHub:
    python install.py

Operazioni:
  1. Installa le dipendenze Python (requirements.txt)
  2. Scarica LibreHardwareMonitorLib.dll + RAMSPDToolkit-NDD.dll in tools/LibreHardwareMonitor/
  3. Compila core/SensorBridge.cs -> tools/LibreHardwareMonitor/SensorBridge.exe
  4. Crea il collegamento sul Desktop e abilita l'avvio automatico
"""
import ctypes
import logging
import os
import shutil
import subprocess
import sys
import urllib.request
import winreg
import zipfile

BASE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(BASE, "tools")
LHM_DIR = os.path.join(TOOLS, "LibreHardwareMonitor")
CONFIG = os.path.join(BASE, "config")

logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
log = logging.getLogger("install")

LHM_URL = "https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip"
PYTHON_PACKAGES = ["customtkinter", "wmi", "psutil", "pillow"]


def step(msg: str):
    log.info(f"\n{'─'*55}\n  {msg}\n{'─'*55}")


def ok(msg: str):
    log.info(f"  ✅  {msg}")


def warn(msg: str):
    log.info(f"  ⚠   {msg}")


def install_packages():
    step("1/4 – Installazione dipendenze Python")
    for pkg in PYTHON_PACKAGES:
        try:
            __import__(pkg)
            ok(f"{pkg} già installato.")
        except ImportError:
            log.info(f"  📥  Installo {pkg}...")
            subprocess.check_call(
                [sys.executable, "-m", "pip", "install", pkg, "-q"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            ok(f"{pkg} installato.")


def setup_sensor_libraries():
    step("2/4 – Scaricamento librerie sensori (LibreHardwareMonitorLib)")
    dll_path = os.path.join(LHM_DIR, "LibreHardwareMonitorLib.dll")
    if os.path.exists(dll_path):
        ok("Librerie sensori già presenti.")
        return

    os.makedirs(LHM_DIR, exist_ok=True)
    zip_path = os.path.join(TOOLS, "LibreHardwareMonitor.zip")
    log.info("  ⬇  Download librerie ufficiali...")
    req = urllib.request.Request(LHM_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    with open(zip_path, "wb") as f:
        f.write(data)

    with zipfile.ZipFile(zip_path) as z:
        z.extractall(LHM_DIR)

    # Rimuovi zip, eseguibile GUI e file di traduzione non necessari
    for leftover in ["LibreHardwareMonitor.zip"]:
        p = os.path.join(TOOLS, leftover)
        if os.path.exists(p):
            os.remove(p)
    for fname in [
        "LibreHardwareMonitor.exe",
        "LibreHardwareMonitor.exe.config",
        "Aga.Controls.pdb",
        "LibreHardwareMonitorLib.pdb",
        "LibreHardwareMonitorLib.xml",
    ]:
        p = os.path.join(LHM_DIR, fname)
        if os.path.exists(p):
            os.remove(p)
    for loc in ["de", "es", "fr", "it", "ja", "pl", "ru", "sv", "tr", "zh-CN", "zh-Hant"]:
        d = os.path.join(LHM_DIR, loc)
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)

    ok("Librerie estratte e ripulite.")


def compile_sensor_bridge():
    step("3/4 – Compilazione SensorBridge.exe")
    out_exe = os.path.join(LHM_DIR, "SensorBridge.exe")
    src_cs = os.path.join(BASE, "core", "SensorBridge.cs")
    lhm_dll = os.path.join(LHM_DIR, "LibreHardwareMonitorLib.dll")
    ram_dll = os.path.join(LHM_DIR, "RAMSPDToolkit-NDD.dll")

    csc_candidates = [
        r"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
        r"C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe",
    ]
    csc = next((p for p in csc_candidates if os.path.exists(p)), None)
    if not csc:
        warn("Compilatore .NET (csc.exe) non trovato. SensorBridge non compilato.")
        return

    cmd = [
        csc,
        "/nologo",
        "/optimize",
        "/platform:x64",
        f"/reference:{lhm_dll}",
        f"/reference:{ram_dll}",
        f"/out:{out_exe}",
        src_cs,
    ]
    subprocess.check_call(cmd)
    ok(f"SensorBridge.exe compilato in {out_exe}")


def setup_shortcuts_and_autorun():
    step("4/4 – Collegamento Desktop & Avvio automatico")
    python = sys.executable
    launcher_py = os.path.join(BASE, "launcher.py")
    desktop = os.path.join(os.path.expanduser("~"), "Desktop")
    vbs_path = os.path.join(desktop, "RGBController (Admin).vbs")

    q = '"'
    vbs = (
        'Set oShell = CreateObject("Shell.Application")\n'
        f'oShell.ShellExecute {q}{python}{q}, {q}{launcher_py}{q}, {q}{BASE}{q}, "runas", 1\n'
    )
    with open(vbs_path, "w", encoding="utf-8") as f:
        f.write(vbs)
    ok(f"Collegamento Desktop creato: {vbs_path}")

    try:
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Run",
            0,
            winreg.KEY_SET_VALUE,
        )
        winreg.SetValueEx(key, "RGBController", 0, winreg.REG_SZ, f'"{python}" "{launcher_py}"')
        winreg.CloseKey(key)
        ok("Avvio automatico registrato in HKCU\\...\\Run\\RGBController")
    except Exception as exc:
        warn(f"Impossibile registrare autorun: {exc}")


def main():
    print("\n╔══════════════════════════════════════════════════════╗")
    print("║      RGBController – Setup Automatico                ║")
    print("╚══════════════════════════════════════════════════════╝")

    os.makedirs(TOOLS, exist_ok=True)
    os.makedirs(CONFIG, exist_ok=True)

    install_packages()
    setup_sensor_libraries()
    compile_sensor_bridge()
    setup_shortcuts_and_autorun()

    print("\n✅ Installazione completata! Avvia l'app con Avvia.bat o 'RGBController (Admin).vbs'.\n")


if __name__ == "__main__":
    main()
