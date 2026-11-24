"""
core/drivers/asrock_hid.py – Driver nativo Python per schede madri ASRock Polychrome USB (VID 0x26CE).
"""
from __future__ import annotations
import logging
from typing import Optional
from core.drivers.hid_win import enumerate_hid, open_hid_rw, close_hid, write_output

logger = logging.getLogger(__name__)

ASROCK_PIDS = {0x01A2, 0x01A4, 0x01A6}


def _get_mb_name() -> str:
    try:
        import wmi
        c = wmi.WMI(namespace="root/cimv2")
        for mb in c.Win32_BaseBoard():
            if mb.Product:
                return mb.Product.strip()
    except Exception:
        pass
    return "Scheda Madre ASRock Polychrome"


class AsrockDriver:
    VID = 0x26CE

    def __init__(self):
        self._path: Optional[str] = None
        self._out_len = 64
        self.name = "Scheda Madre ASRock Polychrome"
        self.zones = [
            {"index": 0, "name": "Polychrome Mainboard", "leds": 16, "resizable": False},
            {"index": 1, "name": "Addressable LED Header", "leds": 32, "resizable": True},
        ]

    def detect(self) -> bool:
        for dev in enumerate_hid():
            if dev.vid == self.VID and dev.pid in ASROCK_PIDS and dev.out_len >= 64:
                self._path = dev.path
                self._out_len = dev.out_len
                self.name = f"{_get_mb_name()} (ASRock Polychrome)"
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
                "Color Cycle": 0x04,
                "Rainbow": 0x05,
                "Wave": 0x05,
                "Chase": 0x06,
            }
            mode_id = mode_map.get(effect, 0x01)
            pkt = bytearray(self._out_len)
            pkt[0] = 0x00
            pkt[1] = 0x11  # Set zone color/mode
            pkt[2] = 0xFF  # all zones
            pkt[3] = mode_id
            pkt[4] = sr
            pkt[5] = sg
            pkt[6] = sb
            pkt[7] = 0x03  # speed
            pkt[8] = max(0, min(255, int(round(brightness * 255 / 100.0))))
            return write_output(h, bytes(pkt))
        finally:
            close_hid(h)

    def set_zone(self, _zone_idx: int, r: int, g: int, b: int, brightness: int = 100, effect: str = "Static") -> bool:
        return self.set_effect(effect, r, g, b, brightness)
