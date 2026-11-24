"""
core/drivers/asus_aura_hid.py – Driver nativo Python per schede madri ASUS Aura Sync USB (VID 0x0B05).
"""
from __future__ import annotations
import logging
from typing import Optional
from core.drivers.hid_win import enumerate_hid, open_hid_rw, close_hid, write_output

logger = logging.getLogger(__name__)

ASUS_AURA_PIDS = {0x18F3, 0x1939, 0x19AF, 0x1872, 0x1867, 0x18A3, 0x19B6, 0x1A68}


def _get_mb_name() -> str:
    try:
        import wmi
        c = wmi.WMI(namespace="root/cimv2")
        for mb in c.Win32_BaseBoard():
            if mb.Product:
                return mb.Product.strip()
    except Exception:
        pass
    return "Scheda Madre ASUS Aura"


class AsusAuraDriver:
    VID = 0x0B05

    def __init__(self):
        self._path: Optional[str] = None
        self._out_len = 65
        self.name = "Scheda Madre ASUS Aura"
        self.zones = [
            {"index": 0, "name": "Aura Mainboard", "leds": 32, "resizable": False},
            {"index": 1, "name": "Aura ARGB Header 1", "leds": 32, "resizable": True},
            {"index": 2, "name": "Aura ARGB Header 2", "leds": 32, "resizable": True},
        ]

    def detect(self) -> bool:
        for dev in enumerate_hid():
            if dev.vid == self.VID and dev.pid in ASUS_AURA_PIDS and dev.out_len in (64, 65):
                self._path = dev.path
                self._out_len = dev.out_len
                self.name = f"{_get_mb_name()} (ASUS Aura)"
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

            # Pacchetto Aura Effect (0xEC, 0x35)
            pkt = bytearray(self._out_len)
            pkt[0] = 0xEC
            pkt[1] = 0x35
            pkt[2] = 0x00  # zone
            pkt[3] = 0x00
            pkt[4] = 0x00
            pkt[5] = mode_id
            pkt[6] = sr
            pkt[7] = sg
            pkt[8] = sb
            ok = write_output(h, bytes(pkt))

            # Commit (0xEC, 0x3F)
            commit = bytearray(self._out_len)
            commit[0] = 0xEC
            commit[1] = 0x3F
            commit[2] = 0x55
            write_output(h, bytes(commit))
            return ok
        finally:
            close_hid(h)

    def set_zone(self, _zone_idx: int, r: int, g: int, b: int, brightness: int = 100, effect: str = "Static") -> bool:
        return self.set_effect(effect, r, g, b, brightness)
