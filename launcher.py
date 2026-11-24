"""
launcher.py – Avvio diretto di RGBController (100% autonomo, senza OpenRGB.exe o LHM.exe).
"""
import sys
import os
import logging
import ctypes

BASE = os.path.dirname(os.path.abspath(__file__))
MAIN_PY = os.path.join(BASE, "main.py")

os.makedirs(os.path.join(BASE, "config"), exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(os.path.join(BASE, "config", "launcher.log"), encoding="utf-8"),
    ]
)
log = logging.getLogger("launcher")


def is_admin() -> bool:
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def main():
    log.info("RGBController Launcher (Admin=%s)", is_admin())
    if not is_admin():
        log.warning("Avviato senza privilegi Amministratore: alcune ventole della scheda madre potrebbero non essere visibili.")
    python = sys.executable
    os.chdir(BASE)
    os.execv(python, [python, MAIN_PY])


if __name__ == "__main__":
    main()
