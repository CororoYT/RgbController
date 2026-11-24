"""
core/drivers/logitech_g213_hid.py – Driver nativo Python per periferiche Logitech HID++ 2.0 RGB.
"""
from __future__ import annotations
import logging
from typing import Optional
from core.drivers.hid_win import enumerate_hid, open_hid_rw, close_hid, write_output

logger = logging.getLogger(__name__)

_LOGITECH_MODELS = {
    0xC336: "Logitech G213 Prodigy",
    0xC33C: "Logitech G513",
    0xC333: "Logitech G610",
    0xC33A: "Logitech G413",
    0xC33F: "Logitech G815",
}


class LogitechG213Driver:
    VID = 0x046D

    def __init__(self):
        self._path: Optional[str] = None
        self.name = "Tastiera Logitech RGB"

    def detect(self) -> bool:
        for dev in enumerate_hid():
            if dev.vid == self.VID and dev.pid in _LOGITECH_MODELS and dev.usage_page == 0xFF43 and dev.out_len == 20:
                self._path = dev.path
                self.name = _LOGITECH_MODELS[dev.pid]
                logger.info("Rilevato driver nativo: %s", self.name)
                return True
        return False

    @staticmethod
    def _scale(r: int, g: int, b: int, brightness: int) -> tuple[int, int, int]:
        f = max(0.0, min(1.0, brightness / 100.0))
        return int(r * f), int(g * f), int(b * f)

    def set_effect(self, effect: str, r: int, g: int, b: int, brightness: int = 100) -> bool:
        if not self._path:
            return False
        h = open_hid_rw(self._path)
        if not h:
            return False
        try:
            sr, sg, sb = self._scale(r, g, b, brightness)
            pkt = bytearray(20)
            pkt[0] = 0x11
            pkt[1] = 0xFF
            pkt[2] = 0x0C
            pkt[3] = 0x3A
            pkt[4] = 0x00

            if effect in ("Color Cycle", "Rainbow", "Wave"):
                pkt[5] = 0x02
                pkt[10] = 0x0F
                pkt[11] = 0xA0
                pkt[12] = max(1, min(100, brightness))
            elif effect == "Breathing":
                pkt[5] = 0x03
                pkt[6] = sr
                pkt[7] = sg
                pkt[8] = sb
                pkt[10] = 0x0F
                pkt[11] = 0xA0
                pkt[12] = max(1, min(100, brightness))
            else:
                pkt[5] = 0x01
                pkt[6] = sr
                pkt[7] = sg
                pkt[8] = sb
                pkt[9] = 0x02

            return write_output(h, bytes(pkt))
        finally:
            close_hid(h)
