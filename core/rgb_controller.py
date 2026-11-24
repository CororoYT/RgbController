"""
rgb_controller.py – Controller RGB 100% nativo (Windows HID diretto + SMBus RAM via SensorBridge).
"""
from __future__ import annotations
import logging
from core.drivers.it5702_hid import IT5702Driver, IT5702_ZONES
from core.drivers.logitech_g213_hid import LogitechG213Driver
from core.sensor_bridge import SensorBridgeClient

logger = logging.getLogger(__name__)

EFFECTS = ["Static", "Breathing", "Rainbow", "Color Cycle", "Chase", "Wave", "Flicker"]


class RGBController:
    """Gestisce tutti i dispositivi RGB tramite driver HID nativi e SMBus."""

    def __init__(self):
        self._mobo = IT5702Driver()
        self._g213 = LogitechG213Driver()
        self._bridge = SensorBridgeClient()
        self._devices: list[dict] = []
        self._connected = False

    def connect(self, _host: str = "127.0.0.1", _port: int = 6742) -> bool:
        self._devices.clear()
        next_id = 0

        if self._mobo.detect():
            self._devices.append({
                "id": next_id,
                "driver": "it5702",
                "name": self._mobo.chip_name,
                "type": "MOBO",
                "zones": IT5702_ZONES,
            })
            next_id += 1

        if self._g213.detect():
            self._devices.append({
                "id": next_id,
                "driver": "g213",
                "name": self._g213.name,
                "type": "PERIPHERAL",
                "zones": [{"index": 0, "name": "Keyboard (5 zone)", "leds": 5, "resizable": False}],
            })
            next_id += 1

        # Verifica se SensorBridge ha rilevato RAM RGB su bus SMBus
        snap = self._bridge.get_snapshot()
        if snap.get("ram_rgb"):
            self._devices.append({
                "id": next_id,
                "driver": "ram_smbus",
                "name": "RAM RGB (SMBus)",
                "type": "RAM",
                "zones": [{"index": 0, "name": "Moduli RAM", "leds": 8, "resizable": False}],
            })
            next_id += 1

        self._connected = len(self._devices) > 0
        logger.info("Motore RGB nativo pronto: %d dispositivi rilevati", len(self._devices))
        return self._connected

    @property
    def is_connected(self) -> bool:
        return self._connected

    @property
    def native_devices(self) -> list[dict]:
        return self._devices

    def _find_dev(self, device_id: int) -> dict | None:
        return next((d for d in self._devices if d["id"] == device_id), None)

    def resize_zone(self, _device_id: int, _zone_idx: int, _led_count: int) -> bool:
        return True

    def set_zone_color(
        self, device_id: int, zone_idx: int, r: int, g: int, b: int, brightness: int = 100
    ) -> bool:
        dev = self._find_dev(device_id)
        if not dev:
            return False
        if dev["driver"] == "it5702":
            return self._mobo.set_zone(zone_idx, r, g, b, brightness, "Static")
        if dev["driver"] == "g213":
            return self._g213.set_effect("Static", r, g, b, brightness)
        if dev["driver"] == "ram_smbus":
            return self._bridge.set_ram_rgb("Static", r, g, b, brightness)
        return False

    def set_color(self, device_id: int, r: int, g: int, b: int, brightness: int = 100) -> bool:
        return self.set_effect(device_id, "Static", r, g, b, brightness)

    def set_effect(
        self,
        device_id: int,
        effect_name: str,
        r: int = 255,
        g: int = 255,
        b: int = 255,
        brightness: int = 100,
    ) -> bool:
        dev = self._find_dev(device_id)
        if not dev:
            return False
        if dev["driver"] == "it5702":
            return self._mobo.set_all(r, g, b, brightness, effect_name)
        if dev["driver"] == "g213":
            return self._g213.set_effect(effect_name, r, g, b, brightness)
        if dev["driver"] == "ram_smbus":
            return self._bridge.set_ram_rgb(effect_name, r, g, b, brightness)
        return False

    def set_all_off(self) -> None:
        for dev in self._devices:
            self.set_effect(dev["id"], "Static", 0, 0, 0, 0)

    def disconnect(self) -> None:
        self._connected = False
