"""
sensor_bridge.py – Client Python per il nostro servizio nativo SensorBridge.exe.
Legge ventole, temperature, carichi CPU/GPU, controlla il duty delle ventole
e gestisce i LED delle RAM su bus SMBus.
"""
from __future__ import annotations
import json
import logging
import subprocess
import threading
from pathlib import Path
from typing import Any, Optional

logger = logging.getLogger(__name__)

BRIDGE_EXE = (
    Path(__file__).resolve().parent.parent
    / "tools"
    / "LibreHardwareMonitor"
    / "SensorBridge.exe"
)


class SensorBridgeClient:
    """Singleton thread-safe che gestisce il processo figlio SensorBridge.exe."""

    _instance: Optional["SensorBridgeClient"] = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        self._proc: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._available = BRIDGE_EXE.exists()
        self._has_ram_rgb = False

    @property
    def is_available(self) -> bool:
        return self._available

    @property
    def has_ram_rgb(self) -> bool:
        return self._has_ram_rgb

    def _ensure_started(self) -> bool:
        if not self._available:
            return False
        if self._proc is not None and self._proc.poll() is None:
            return True
        try:
            self._proc = subprocess.Popen(
                [str(BRIDGE_EXE)],
                cwd=str(BRIDGE_EXE.parent),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            ready_line = self._proc.stdout.readline()  # type: ignore[union-attr]
            if not ready_line:
                self.stop()
                return False
            data = json.loads(ready_line)
            if data.get("ready"):
                logger.info("SensorBridge avviato correttamente.")
                return True
            if "error" in data:
                logger.warning("SensorBridge errore avvio: %s", data["error"])
            self.stop()
            return False
        except Exception as exc:
            logger.error("Impossibile avviare SensorBridge: %s", exc)
            self.stop()
            return False

    def _send_command(self, cmd: str) -> Optional[dict[str, Any]]:
        with self._lock:
            if not self._ensure_started():
                return None
            try:
                assert self._proc is not None
                assert self._proc.stdin is not None
                assert self._proc.stdout is not None
                self._proc.stdin.write(cmd + "\n")
                self._proc.stdin.flush()
                line = self._proc.stdout.readline()
                if not line:
                    self.stop()
                    return None
                return json.loads(line)
            except Exception as exc:
                logger.error("Errore comunicazione SensorBridge (%s): %s", cmd, exc)
                self.stop()
                return None

    def get_snapshot(self) -> dict[str, Any]:
        res = self._send_command("GET")
        if res and "fans" in res:
            self._has_ram_rgb = bool(res.get("ram_rgb", False))
            return res
        return {"fans": [], "temps": [], "loads": [], "ram_rgb": False}

    def set_fan_duty(self, control_id: str, duty: float) -> bool:
        if not control_id:
            return False
        res = self._send_command(f"SET {control_id} {duty:.1f}")
        return bool(res and res.get("ok"))

    def set_fan_auto(self, control_id: str) -> bool:
        if not control_id:
            return False
        res = self._send_command(f"AUTO {control_id}")
        return bool(res and res.get("ok"))

    def set_ram_rgb(self, effect: str, r: int, g: int, b: int, brightness: int) -> bool:
        mode_tok = effect.replace(" ", "_")
        res = self._send_command(f"RAMRGB {mode_tok} {r} {g} {b} {brightness}")
        return bool(res and res.get("ok"))

    def stop(self) -> None:
        if self._proc is not None:
            try:
                if self._proc.poll() is None and self._proc.stdin:
                    self._proc.stdin.write("QUIT\n")
                    self._proc.stdin.flush()
                    self._proc.wait(timeout=2)
            except Exception:
                try:
                    self._proc.kill()
                except Exception:
                    pass
            finally:
                self._proc = None
