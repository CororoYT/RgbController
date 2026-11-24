"""
app_controller.py – Orchestratore centrale.
Coordina detector, RGB controller, fan controller, SensorBridge e settings manager.
"""
from __future__ import annotations
import logging
import os
import signal
import threading
from typing import Callable, Optional

from core.hardware_detector import HardwareDetector, HardwareSnapshot
from core.rgb_controller import RGBController
from core.fan_controller import FanController
from core.sensor_bridge import SensorBridgeClient
from core.settings_manager import SettingsManager

logger = logging.getLogger(__name__)


class AppController:
    """Punto di accesso unico per la GUI."""

    def __init__(self):
        self.settings = SettingsManager()
        self.detector = HardwareDetector()
        self.rgb_ctrl = RGBController()
        self.fan_ctrl = FanController()
        self._bridge = SensorBridgeClient()

        self._refresh_interval = self.settings.get("refresh_interval_ms", 2000) / 1000.0
        self._refresh_timer: Optional[threading.Timer] = None
        self._stopped = False
        self._on_snapshot_update: list[Callable[[HardwareSnapshot], None]] = []
        self.on_openrgb_status: Optional[Callable[[bool], None]] = None

    def start(self) -> HardwareSnapshot:
        connected = self.rgb_ctrl.connect()
        if self.on_openrgb_status:
            self.on_openrgb_status(connected)

        snapshot = self.detector.detect_all(self.rgb_ctrl)
        self._apply_saved_settings(snapshot)
        self._schedule_refresh()
        return snapshot

    def _apply_saved_settings(self, snapshot: HardwareSnapshot) -> None:
        startup = self.settings.get("startup_profile")
        if startup:
            self.settings.load_profile(startup)

        for dev in snapshot.rgb_devices:
            if not self.rgb_ctrl.is_connected:
                continue

            # 1) Ridimensiona eventuali header ARGB (es. D_LED1, D_LED2)
            saved_sizes = self.settings.get_zone_sizes(dev.id)
            for z in dev.zones:
                target_leds = saved_sizes.get(z.index)
                if target_leds is None and z.resizable and z.led_count == 0:
                    target_leds = 24
                    self.settings.save_zone_size(dev.id, z.index, target_leds)
                if target_leds is not None and target_leds != z.led_count:
                    if self.rgb_ctrl.resize_zone(dev.id, z.index, target_leds):
                        z.led_count = target_leds

            # 2) Applica effetto/colore/luminosità salvati
            cfg = self.settings.get_device_settings(dev.id)
            r, g, b = cfg["color"]
            brightness = cfg["brightness"]
            effect = cfg["effect"]
            self.rgb_ctrl.set_effect(dev.id, effect, r, g, b, brightness)

            # 3) Applica eventuali colori specifici per singola zona
            for z_idx, z_col in self.settings.get_zone_colors(dev.id).items():
                if len(z_col) == 3:
                    self.rgb_ctrl.set_zone_color(dev.id, z_idx, z_col[0], z_col[1], z_col[2], brightness)

        for fan in snapshot.fans:
            cfg = self.settings.get_fan_settings(fan.id)
            if cfg["mode"] == "manual" and fan.controllable and fan.control_id:
                self.fan_ctrl.set_fan_duty(fan.control_id, cfg["duty_percent"])

    def _schedule_refresh(self) -> None:
        if self._stopped:
            return
        self._refresh_timer = threading.Timer(
            self._refresh_interval, self._do_refresh
        )
        self._refresh_timer.daemon = True
        self._refresh_timer.start()

    def _do_refresh(self) -> None:
        if self._stopped:
            return
        try:
            self.detector.refresh_sensors()
            snap = self.detector.get_snapshot()
            for cb in self._on_snapshot_update:
                try:
                    cb(snap)
                except Exception:
                    pass
        finally:
            self._schedule_refresh()

    def stop(self) -> None:
        self._stopped = True
        if self._refresh_timer:
            self._refresh_timer.cancel()
        self.rgb_ctrl.disconnect()
        self._bridge.stop()
        managed_pid = os.environ.get("RGBCTRL_OPENRGB_PID")
        if managed_pid:
            try:
                os.kill(int(managed_pid), signal.SIGTERM)
            except Exception:
                pass

    def register_snapshot_callback(self, cb: Callable[[HardwareSnapshot], None]) -> None:
        self._on_snapshot_update.append(cb)

    def reload_rgb(self) -> HardwareSnapshot:
        """Riconnette il motore RGB nativo, riscansiona i dispositivi e riapplica le luci salvate."""
        connected = self.rgb_ctrl.connect()
        if self.on_openrgb_status:
            self.on_openrgb_status(connected)
        snapshot = self.detector.detect_all(self.rgb_ctrl)
        self._apply_saved_settings(snapshot)
        return snapshot

    def apply_rgb(self, device_id: int, r: int, g: int, b: int,
                  brightness: int = 100, effect: str = "Static") -> None:
        self.rgb_ctrl.set_effect(device_id, effect, r, g, b, brightness)
        self.settings.save_device_settings(device_id, [r, g, b], brightness, effect)

    def apply_zone_rgb(self, device_id: int, zone_idx: int, r: int, g: int, b: int,
                       brightness: int = 100) -> None:
        self.rgb_ctrl.set_zone_color(device_id, zone_idx, r, g, b, brightness)
        self.settings.save_zone_color(device_id, zone_idx, [r, g, b])

    def resize_zone(self, device_id: int, zone_idx: int, led_count: int) -> bool:
        ok = self.rgb_ctrl.resize_zone(device_id, zone_idx, led_count)
        if ok:
            self.settings.save_zone_size(device_id, zone_idx, led_count)
            snap = self.detector.get_snapshot()
            for dev in snap.rgb_devices:
                if dev.id == device_id:
                    for z in dev.zones:
                        if z.index == zone_idx:
                            z.led_count = led_count
                    dev.led_count = sum(z.led_count for z in dev.zones)
        return ok

    def apply_rgb_all(self, r: int, g: int, b: int,
                      brightness: int = 100, effect: str = "Static") -> None:
        snap = self.detector.get_snapshot()
        for dev in snap.rgb_devices:
            self.apply_rgb(dev.id, r, g, b, brightness, effect)

    def set_fan_duty(self, fan_id: str, control_id: str, duty: int, mode: str = "manual") -> bool:
        if mode == "auto":
            ok = self.fan_ctrl.set_fan_auto(control_id)
        else:
            ok = self.fan_ctrl.set_fan_duty(control_id, duty)
        self.settings.save_fan_settings(fan_id, duty, mode)
        return ok

    def set_fan_label(self, fan_id: str, label: str) -> None:
        self.settings.save_fan_label(fan_id, label)

    def save_profile(self, name: str) -> None:
        self.settings.save_profile(name)

    def load_profile(self, name: str) -> bool:
        ok = self.settings.load_profile(name)
        if ok:
            snap = self.detector.get_snapshot()
            self._apply_saved_settings(snap)
        return ok

    def list_profiles(self) -> list[str]:
        return self.settings.list_profiles()

    def delete_profile(self, name: str) -> None:
        self.settings.delete_profile(name)
