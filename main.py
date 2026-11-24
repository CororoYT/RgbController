"""
main.py – Entry point dell'applicazione RGBController.
"""
import logging
import os
import sys
import threading

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_DIR = os.path.join(BASE_DIR, "config")
os.makedirs(CONFIG_DIR, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s – %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(os.path.join(CONFIG_DIR, "app.log"), encoding="utf-8"),
    ],
)

from core.app_controller import AppController
from gui.main_window import MainWindow


def main():
    ctrl = AppController()
    app = MainWindow(ctrl)
    app.protocol("WM_DELETE_WINDOW", app.on_close)

    def _startup():
        snap = ctrl.start()
        app.after(0, lambda: _post_startup(snap))

    def _post_startup(snap):
        if "rgb" in app._pages:
            app._pages["rgb"].refresh()  # type: ignore[attr-defined]
        if "fans" in app._pages:
            app._pages["fans"]._load_snapshot(snap)  # type: ignore[attr-defined]
        if "cooler" in app._pages:
            app._pages["cooler"].update_snapshot(snap)  # type: ignore[attr-defined]
        app._update_status_bar()

    t = threading.Thread(target=_startup, daemon=True)
    t.start()

    app.mainloop()


if __name__ == "__main__":
    main()
