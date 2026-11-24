"""
core/drivers/it5702_hid.py – Driver nativo Python per schede madri Gigabyte RGB Fusion 2.0 (IT5702 / IT8297).
Protocollo HID verificato:
  - 0x34: LED count per header ARGB (0x11 = 64 LED su D_LED1 e D_LED2)
  - 0x32: bitmask Direct ARGB (0x03 = D_LED1 | D_LED2, 0x00 = Hardware mode) + 50ms wait
  - 0x20..0x27 + 0x28: zone scheda madre ed effetti hardware (brightness 0..255)
  - 0x58 / 0x59: Direct ARGB per D_LED1 / D_LED2 (RGB 0..255)
"""
from __future__ import annotations
import logging
import time
from typing import Optional
from core.drivers.hid_win import enumerate_hid, open_hid_rw, close_hid, set_feature, get_feature

logger = logging.getLogger(__name__)

IT5702_ZONES = [
    {"index": 0, "hw_idx": 5, "name": "D_LED1", "leds": 64, "resizable": True},
    {"index": 1, "hw_idx": 6, "name": "D_LED2", "leds": 64, "resizable": True},
    {"index": 2, "hw_idx": 0, "name": "LED Zona 1 (I/O)", "leds": 1, "resizable": False},
    {"index": 3, "hw_idx": 1, "name": "LED_C1 (12V)", "leds": 1, "resizable": False},
    {"index": 4, "hw_idx": 2, "name": "LED Zona 2 (PCIe)", "leds": 1, "resizable": False},
    {"index": 5, "hw_idx": 3, "name": "LED_C2 (12V)", "leds": 1, "resizable": False},
    {"index": 6, "hw_idx": 4, "name": "LED Zona 3", "leds": 1, "resizable": False},
    {"index": 7, "hw_idx": 7, "name": "LED Zona 4", "leds": 1, "resizable": False},
]

_EFFECT_MAP = {
    "Static": 0x01,
    "Breathing": 0x02,
    "Flicker": 0x03,
    "Chase": 0x03,
    "Color Cycle": 0x04,
    "Rainbow": 0x04,
    "Wave": 0x06,
}


def _get_motherboard_name() -> str:
    try:
        import wmi
        c = wmi.WMI(namespace="root/cimv2")
        for mb in c.Win32_BaseBoard():
            if mb.Product:
                return mb.Product.strip()
    except Exception:
        pass
    return "Scheda Madre Gigabyte RGB"


class IT5702Driver:
    VID = 0x048D
    PIDS = (0x5702, 0x8297)

    def __init__(self):
        self._path: Optional[str] = None
        self.chip_name: str = "Scheda Madre Gigabyte RGB"

    def detect(self) -> bool:
        for dev in enumerate_hid():
            if dev.vid == self.VID and dev.pid in self.PIDS and dev.feat_len == 64:
                self._path = dev.path
                mb_name = _get_motherboard_name()
                chip_tag = f"IT{dev.pid:04X}"
                h = open_hid_rw(dev.path)
                if h:
                    try:
                        pkt = bytearray(64)
                        pkt[0] = 0xCC
                        pkt[1] = 0x60
                        set_feature(h, bytes(pkt))
                        resp = get_feature(h, 0xCC, 64)
                        if resp and b"IT" in resp:
                            raw = resp[12:36].split(b"\x00", 1)[0].decode("ascii", errors="ignore").strip()
                            if raw:
                                chip_tag = raw
                    finally:
                        close_hid(h)
                self.chip_name = f"{mb_name} ({chip_tag})"
                logger.info("Rilevato controller nativo: %s", self.chip_name)
                return True
        return False

    def _set_direct_mask(self, handle: int, mask: int) -> None:
        p32 = bytearray(64)
        p32[0] = 0xCC
        p32[1] = 0x32
        p32[2] = mask & 0xFF
        set_feature(handle, bytes(p32))
        time.sleep(0.05)  # 50ms richiesti dal firmware IT5702

    def _set_argb_led_count(self, handle: int) -> None:
        # 0x34: (new_count[1] << 4) | new_count[0] -> 0x11 = 64 LED su D_LED1 e D_LED2
        p34 = bytearray(64)
        p34[0] = 0xCC
        p34[1] = 0x34
        p34[2] = 0x11
        set_feature(handle, bytes(p34))
        time.sleep(0.01)

    def _apply_hw(self, handle: int, zone_mask: int = 0xFF) -> bool:
        pkt = bytearray(64)
        pkt[0] = 0xCC
        pkt[1] = 0x28
        pkt[2] = zone_mask & 0xFF
        ok = set_feature(handle, bytes(pkt))
        time.sleep(0.025)
        return ok

    def _send_direct_argb(
        self, handle: int, cmd: int, r: int, g: int, b: int, brightness: int, led_count: int = 64
    ) -> bool:
        f = max(0.0, min(1.0, brightness / 100.0))
        sr, sg, sb = int(round(r * f)), int(round(g * f)), int(round(b * f))
        ok = True
        offset = 0
        remaining = max(1, min(128, led_count))
        while remaining > 0:
            chunk = min(19, remaining)
            pd = bytearray(64)
            pd[0] = 0xCC
            pd[1] = cmd  # 0x58 = D_LED1, 0x59 = D_LED2
            pd[2] = (offset * 3) & 0xFF
            pd[3] = ((offset * 3) >> 8) & 0xFF
            pd[4] = chunk * 3
            for i in range(chunk):
                pd[5 + i * 3 + 0] = sg
                pd[5 + i * 3 + 1] = sr
                pd[5 + i * 3 + 2] = sb
            ok &= set_feature(handle, bytes(pd))
            time.sleep(0.004)
            offset += chunk
            remaining -= chunk
        return ok

    def _send_zone_effect(
        self,
        handle: int,
        hw_idx: int,
        mode_id: int,
        r: int,
        g: int,
        b: int,
        brightness: int,
    ) -> bool:
        pkt = bytearray(64)
        pkt[0] = 0xCC
        pkt[1] = 0x20 + hw_idx
        pkt[2] = 1 << hw_idx
        pkt[11] = mode_id
        pkt[12] = max(0, min(255, int(round(brightness * 255 / 100.0))))
        pkt[13] = 0x00
        pkt[14] = b & 0xFF
        pkt[15] = g & 0xFF
        pkt[16] = r & 0xFF
        pkt[22] = 0xC8
        pkt[23] = 0x00
        ok = set_feature(handle, bytes(pkt))
        time.sleep(0.004)
        return ok

    def set_zone(self, zone_idx: int, r: int, g: int, b: int, brightness: int = 100, effect: str = "Static") -> bool:
        if not self._path:
            return False
        zinfo = next((z for z in IT5702_ZONES if z["index"] == zone_idx), None)
        if not zinfo:
            return False
        h = open_hid_rw(self._path)
        if not h:
            return False
        try:
            self._set_argb_led_count(h)
            if effect == "Static" and zone_idx in (0, 1):
                self._set_direct_mask(h, 0x03)
                cmd = 0x58 if zone_idx == 0 else 0x59
                return self._send_direct_argb(h, cmd, r, g, b, brightness, zinfo["leds"])
            self._set_direct_mask(h, 0x00)
            mode_id = _EFFECT_MAP.get(effect, 0x01)
            ok = self._send_zone_effect(h, zinfo["hw_idx"], mode_id, r, g, b, brightness)
            self._apply_hw(h, 1 << zinfo["hw_idx"])
            return ok
        finally:
            close_hid(h)

    def set_all(self, r: int, g: int, b: int, brightness: int = 100, effect: str = "Static") -> bool:
        if not self._path:
            return False
        h = open_hid_rw(self._path)
        if not h:
            return False
        try:
            self._set_argb_led_count(h)
            if effect == "Static":
                # 1) Zone scheda madre in modalità hardware Static
                self._set_direct_mask(h, 0x00)
                ok = True
                for z in IT5702_ZONES:
                    ok &= self._send_zone_effect(h, z["hw_idx"], 0x01, r, g, b, brightness)
                self._apply_hw(h, 0xFF)

                # 2) Header ARGB D_LED1 (0x01) e D_LED2 (0x02) in modalità Direct (0x03)
                self._set_direct_mask(h, 0x03)
                ok &= self._send_direct_argb(h, 0x58, r, g, b, brightness, 64)
                ok &= self._send_direct_argb(h, 0x59, r, g, b, brightness, 64)
                return ok

            self._set_direct_mask(h, 0x00)
            mode_id = _EFFECT_MAP.get(effect, 0x01)
            ok = True
            for z in IT5702_ZONES:
                ok &= self._send_zone_effect(h, z["hw_idx"], mode_id, r, g, b, brightness)
            self._apply_hw(h, 0xFF)
            return ok
        finally:
            close_hid(h)
