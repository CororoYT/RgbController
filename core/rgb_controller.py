"""
rgb_controller.py – Controller RGB 100% nativo e multi-marca:
  - Schede Madri: Gigabyte (IT5702/IT8297), ASUS Aura, MSI Mystic Light, ASRock Polychrome
  - RAM DDR4 & DDR5: HyperX Fury/Predator, ENE/Aura/G.Skill/Kingston DDR5, Corsair Vengeance/Dominator
  - Periferiche: Tastiere Logitech HID++
"""
from __future__ import annotations
import logging
from core.drivers.it5702_hid import IT5702Driver, IT5702_ZONES
from core.drivers.asus_aura_hid import AsusAuraDriver
from core.drivers.msi_mystic_hid import MsiMysticDriver
from core.drivers.asrock_hid import AsrockDriver
from core.drivers.logitech_g213_hid import LogitechG213Driver
from core.sensor_bridge import SensorBridgeClient

logger = logging.getLogger(__name__)

EFFECTS = ["Static", "Breathing", "Rainbow", "Color Cycle", "Chase", "Wave", "Flicker"]


class RGBController:
    """Gestisce tutti i dispositivi RGB tramite driver HID nativi e SMBus."""

    def __init__(self):
        self._mobo_drivers = [
            ("it5702", IT5702Driver()),
            ("asus_aura", AsusAuraDriver()),
            ("msi_mystic", MsiMysticDriver()),
            ("asrock", AsrockDriver()),
        ]
        self._g213 = LogitechG213Driver()
        self._bridge = SensorBridgeClient()
        self._devices: list[dict] = []
        self._active_drivers: dict[str, object] = {}
        self._connected = False

    def connect(self, _host: str = "127.0.0.1", _port: int = 6742) -> bool:
        self._devices.clear()
        self._active_drivers.clear()
        next_id = 0

        # 1) Rileva qualsiasi scheda madre supportata (Gigabyte, ASUS, MSI, ASRock)
        for key, drv in self._mobo_drivers:
            if drv.detect():
                self._active_drivers[key] = drv
                zones = IT5702_ZONES if key == "it5702" else drv.zones
                name = drv.chip_name if key == "it5702" else drv.name
                self._devices.append({
                    "id": next_id,
                    "driver": key,
                    "name": name,
                    "type": "MOBO",
                    "zones": zones,
                })
                next_id += 1

        # 2) Periferiche HID++
        if self._g213.detect():
            self._active_drivers["g213"] = self._g213
            self._devices.append({
                "id": next_id,
                "driver": "g213",
                "name": self._g213.name,
                "type": "PERIPHERAL",
                "zones": [{"index": 0, "name": "Keyboard (5 zone)", "leds": 5, "resizable": False}],
            })
            next_id += 1

        # 3) RAM DDR4 / DDR5 su SMBus tramite SensorBridge
        snap = self._bridge.get_snapshot()
        if snap.get("ram_rgb"):
            ram_name = snap.get("ram_model") or "RAM RGB (SMBus)"
            self._devices.append({
                "id": next_id,
                "driver": "ram_smbus",
                "name": ram_name,
                "type": "RAM",
                "zones": [{"index": 0, "name": "Moduli RAM", "leds": 10, "resizable": False}],
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
        key = dev["driver"]
        if key == "ram_smbus":
            return self._bridge.set_ram_rgb("Static", r, g, b, brightness)
        if key == "g213":
            return self._g213.set_effect("Static", r, g, b, brightness)
        drv = self._active_drivers.get(key)
        if drv and hasattr(drv, "set_zone"):
            return drv.set_zone(zone_idx, r, g, b, brightness, "Static")  # type: ignore[union-attr]
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
        key = dev["driver"]
        if key == "ram_smbus":
            return self._bridge.set_ram_rgb(effect_name, r, g, b, brightness)
        if key == "g213":
            return self._g213.set_effect(effect_name, r, g, b, brightness)
        if key == "it5702":
            return self._active_drivers["it5702"].set_all(r, g, b, brightness, effect_name)  # type: ignore[union-attr]
        drv = self._active_drivers.get(key)
        if drv and hasattr(drv, "set_effect"):
            return drv.set_effect(effect_name, r, g, b, brightness)  # type: ignore[union-attr]
        return False

    def set_all_off(self) -> None:
        for dev in self._devices:
            self.set_effect(dev["id"], "Static", 0, 0, 0, 0)

    def disconnect(self) -> None:
        self._connected = False
