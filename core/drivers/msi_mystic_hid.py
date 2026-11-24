"""
core/drivers/msi_mystic_hid.py – Driver nativo Python per schede madri MSI Mystic Light USB (VID 0x1462).
"""
from __future__ import annotations
import logging
from typing import Optional
from core.drivers.hid_win import enumerate_hid, open_hid_rw, close_hid, set_feature

logger = logging.getLogger(__name__)


def _get_mb_name() -> str:
    try:
        import wmi
        c = wmi.WMI(namespace="root/cimv2")
        for mb in c.Win32_BaseBoard():
            if mb.Product:
                return mb.Product.strip()
    except Exception:
        pass
    return "Scheda Madre MSI Mystic Light"


class MsiMysticDriver:
    VID = 0x1462

    def __init__(self):
        self._path: Optional[str] = None
        self._feat_len = 64
        self.name = "Scheda Madre MSI Mystic Light"
        self.zones = [
            {"index": 0, "name": "Mystic Light Mainboard", "leds": 16, "resizable": False},
            {"index": 1, "name": "JRAINBOW1 (ARGB)", "leds": 32, "resizable": True},
            {"index": 2, "name": "JRAINBOW2 (ARGB)", "leds": 32, "resizable": True},
        ]

    def detect(self) -> bool:
        for dev in enumerate_hid():
            if dev.vid == self.VID and (0x7B00 <= dev.pid <= 0x7F00) and dev.feat_len >= 64:
                self._path = dev.path
                self._feat_len = dev.feat_len
                self.name = f"{_get_mb_name()} (MSI Mystic Light)"
                logger.info("Rilevato controller nativo: %s", self.name)
                return True
        return False

    def set_effect(self, effect: str, r: int, g: int, b: int, brightness: int = 100) -> bool:
        if not self._path:
            return False
        h = open_hid_rw(self._path)
        if not h:
            return False
        try:
            f = max(0.0, min(1.0, brightness / 100.0))
            sr, sg, sb = int(round(r * f)), int(round(g * f)), int(round(b * f))
            mode_map = {
                "Static": 0x01,
                "Breathing": 0x02,
                "Flicker": 0x03,
                "Color Cycle": 0x08,
                "Rainbow": 0x0A,
                "Wave": 0x0A,
                "Chase": 0x09,
            }
            mode_id = mode_map.get(effect, 0x01)
            pkt = bytearray(self._feat_len)
            pkt[0] = 0x52
            pkt[1] = 0x01  # zone all
            pkt[2] = mode_id
            pkt[3] = sr
            pkt[4] = sg
            pkt[5] = sb
            pkt[6] = 0x0A  # speed/brightness flags
            pkt[7] = sr
            pkt[8] = sg
            pkt[9] = sb
            pkt[10] = 0x00
            return set_feature(h, bytes(pkt))
        finally:
            close_hid(h)

    def set_zone(self, _zone_idx: int, r: int, g: int, b: int, brightness: int = 100, effect: str = "Static") -> bool:
        return self.set_effect(effect, r, g, b, brightness)
