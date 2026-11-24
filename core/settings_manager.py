"""
settings_manager.py – Persistenza delle impostazioni su disco (JSON).
"""
from __future__ import annotations
import json
import logging
from copy import deepcopy
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

DEFAULT_LCD_CONFIG: dict[str, Any] = {
    "preset": "Cyberpunk",
    "shape": "circle",
    "resolution": "320x320",
    "bg_mode": "gradient",
    "bg_color": "#0f172a",
    "accent_color": "#38bdf8",
    "text_color": "#f8fafc",
    "image_path": "",
    "show_cpu_temp": True,
    "show_gpu_temp": True,
    "show_cpu_load": True,
    "show_fan_rpm": True,
    "show_clock": True,
    "custom_label": "LIQUID COOLING",
}

DEFAULT_SETTINGS: dict[str, Any] = {
    "version": 4,
    "openrgb_host": "127.0.0.1",
    "openrgb_port": 6742,
    "refresh_interval_ms": 2000,
    "theme": "dark",
    "accent_color": "#1E90FF",
    "rgb_devices": {},     # {device_id: {color, brightness, effect}}
    "zone_sizes": {},      # {device_id: {zone_idx: led_count}}
    "zone_colors": {},     # {device_id: {zone_idx: [r, g, b]}}
    "fans": {},            # {fan_id: {duty_percent, mode}}
    "fan_labels": {},      # {fan_id: "Nome personalizzato"}
    "lcd_config": deepcopy(DEFAULT_LCD_CONFIG),
    "profiles": {},
    "last_profile": None,
    "startup_profile": None,
    "minimize_to_tray": True,
    "autostart": False,
}

SETTINGS_FILE = Path(__file__).parent.parent / "config" / "settings.json"


class SettingsManager:
    """Carica e salva le impostazioni in config/settings.json."""

    def __init__(self, path: Path | None = None):
        self._path = path or SETTINGS_FILE
        self._data: dict[str, Any] = deepcopy(DEFAULT_SETTINGS)
        self.load()

    def load(self) -> None:
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            if self._path.exists():
                with open(self._path, "r", encoding="utf-8") as f:
                    saved = json.load(f)
                self._data = {**DEFAULT_SETTINGS, **saved}
                self._data["lcd_config"] = {
                    **DEFAULT_LCD_CONFIG,
                    **self._data.get("lcd_config", {}),
                }
                logger.info("Impostazioni caricate da %s", self._path)
            else:
                logger.info("Nessun file impostazioni, uso default")
                self.save()
        except Exception as exc:
            logger.error("Errore caricamento impostazioni: %s", exc)

    def save(self) -> None:
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            with open(self._path, "w", encoding="utf-8") as f:
                json.dump(self._data, f, indent=2, ensure_ascii=False)
        except Exception as exc:
            logger.error("Errore salvataggio impostazioni: %s", exc)

    def get(self, key: str, default: Any = None) -> Any:
        return self._data.get(key, default)

    def set(self, key: str, value: Any) -> None:
        self._data[key] = value
        self.save()

    # ---- RGB ----
    def get_device_settings(self, device_id: int) -> dict:
        return deepcopy(
            self._data["rgb_devices"].get(str(device_id), {
                "color": [255, 255, 255],
                "brightness": 100,
                "effect": "Static",
            })
        )

    def save_device_settings(self, device_id: int, color: list[int], brightness: int, effect: str) -> None:
        self._data["rgb_devices"][str(device_id)] = {
            "color": color,
            "brightness": brightness,
            "effect": effect,
        }
        self.save()

    # ---- Zone ARGB (D_LED1, D_LED2, ecc.) ----
    def get_zone_sizes(self, device_id: int) -> dict[int, int]:
        raw = self._data.get("zone_sizes", {}).get(str(device_id), {})
        return {int(k): int(v) for k, v in raw.items()}

    def save_zone_size(self, device_id: int, zone_idx: int, led_count: int) -> None:
        zs = self._data.setdefault("zone_sizes", {})
        dev_zs = zs.setdefault(str(device_id), {})
        dev_zs[str(zone_idx)] = int(led_count)
        self.save()

    def get_zone_colors(self, device_id: int) -> dict[int, list[int]]:
        raw = self._data.get("zone_colors", {}).get(str(device_id), {})
        return {int(k): list(v) for k, v in raw.items()}

    def save_zone_color(self, device_id: int, zone_idx: int, color: list[int]) -> None:
        zc = self._data.setdefault("zone_colors", {})
        dev_zc = zc.setdefault(str(device_id), {})
        dev_zc[str(zone_idx)] = color
        self.save()

    # ---- Ventole ----
    def get_fan_settings(self, fan_id: str) -> dict:
        return deepcopy(
            self._data["fans"].get(fan_id, {
                "duty_percent": 50,
                "mode": "auto",
            })
        )

    def save_fan_settings(self, fan_id: str, duty_percent: int, mode: str = "manual") -> None:
        self._data["fans"][fan_id] = {
            "duty_percent": duty_percent,
            "mode": mode,
        }
        self.save()

    def get_fan_label(self, fan_id: str) -> str | None:
        return self._data.get("fan_labels", {}).get(fan_id)

    def save_fan_label(self, fan_id: str, label: str) -> None:
        labels = self._data.setdefault("fan_labels", {})
        if label.strip():
            labels[fan_id] = label.strip()
        else:
            labels.pop(fan_id, None)
        self.save()

    # ---- LCD AIO ----
    def get_lcd_config(self) -> dict[str, Any]:
        return deepcopy(self._data.get("lcd_config", DEFAULT_LCD_CONFIG))

    def save_lcd_config(self, cfg: dict[str, Any]) -> None:
        self._data["lcd_config"] = deepcopy(cfg)
        self.save()

    # ---- Profili ----
    def save_profile(self, name: str) -> None:
        self._data["profiles"][name] = {
            "rgb_devices": deepcopy(self._data["rgb_devices"]),
            "zone_sizes": deepcopy(self._data.get("zone_sizes", {})),
            "zone_colors": deepcopy(self._data.get("zone_colors", {})),
            "fans": deepcopy(self._data["fans"]),
            "lcd_config": deepcopy(self._data.get("lcd_config", DEFAULT_LCD_CONFIG)),
        }
        self._data["last_profile"] = name
        self.save()

    def load_profile(self, name: str) -> bool:
        if name not in self._data["profiles"]:
            return False
        profile = self._data["profiles"][name]
        self._data["rgb_devices"] = deepcopy(profile.get("rgb_devices", {}))
        if "zone_sizes" in profile:
            self._data["zone_sizes"] = deepcopy(profile["zone_sizes"])
        if "zone_colors" in profile:
            self._data["zone_colors"] = deepcopy(profile["zone_colors"])
        self._data["fans"] = deepcopy(profile.get("fans", {}))
        if "lcd_config" in profile:
            self._data["lcd_config"] = deepcopy(profile["lcd_config"])
        self._data["last_profile"] = name
        self.save()
        return True

    def list_profiles(self) -> list[str]:
        return list(self._data["profiles"].keys())

    def delete_profile(self, name: str) -> None:
        self._data["profiles"].pop(name, None)
        if self._data.get("last_profile") == name:
            self._data["last_profile"] = None
        self.save()
