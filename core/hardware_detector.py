"""
hardware_detector.py – Auto-rilevamento di tutti i dispositivi hardware.
- RGB via OpenRGB SDK (riutilizza il client esistente, legge zone ARGB)
- Ventole, temperature e carichi via SensorBridge nativo (+ fallback nvidia-smi / psutil)
- Riferimenti automatici ai connettori ventole della scheda madre
- Rilevamento AIO LCD USB (es. Thermalright 0416:5302) via WMI
"""
from __future__ import annotations
import logging
import subprocess
import threading
from dataclasses import dataclass, field
from typing import Optional

import psutil

from core.sensor_bridge import SensorBridgeClient

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Strutture dati
# ---------------------------------------------------------------------------

@dataclass
class RGBZoneInfo:
    index: int
    name: str
    led_count: int
    resizable: bool = False


@dataclass
class RGBDevice:
    id: int
    name: str
    type: str          # "RAM", "MOBO", "GPU", "COOLER", "PERIPHERAL", "OTHER"
    zone_count: int
    led_count: int
    zones: list[RGBZoneInfo] = field(default_factory=list)
    active: bool = True
    color: tuple[int, int, int] = (255, 255, 255)
    brightness: int = 100
    effect: str = "Static"


@dataclass
class FanSensor:
    id: str
    name: str
    source: str
    hardware: str = ""
    reference: str = ""
    rpm: int = 0
    target_rpm: int = 0
    duty_percent: int = 50
    control_id: str = ""
    controllable: bool = False


@dataclass
class TempSensor:
    id: str
    name: str
    celsius: float = 0.0
    hardware: str = ""
    source: str = "SensorBridge"


@dataclass
class CoolerDevice:
    name: str
    pnp_id: str
    vid: str
    pid: str
    connected: bool = True


@dataclass
class HardwareSnapshot:
    rgb_devices: list[RGBDevice] = field(default_factory=list)
    fans: list[FanSensor] = field(default_factory=list)
    temps: list[TempSensor] = field(default_factory=list)
    loads: dict[str, float] = field(default_factory=dict)
    coolers: list[CoolerDevice] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Classificazione tipo RGB
# ---------------------------------------------------------------------------

_OPENRGB_TYPE_MAP: dict[str, str] = {
    "DRAM": "RAM",
    "GPU": "GPU",
    "MOTHERBOARD": "MOBO",
    "COOLER": "COOLER",
    "LEDSTRIP": "COOLER",
    "KEYBOARD": "PERIPHERAL",
    "MOUSE": "PERIPHERAL",
    "MOUSEMAT": "PERIPHERAL",
    "HEADSET": "PERIPHERAL",
    "HEADSET_STAND": "PERIPHERAL",
    "GAMEPAD": "PERIPHERAL",
}

_TYPE_KEYWORDS: dict[str, list[str]] = {
    "RAM": [
        "dram", "dimm", "vengeance", "trident", "fury", "hyperx",
        "kingston", "predator", "ballistix", "dominator", "ripjaws",
        "spectrix", "ene dram", "crucial",
    ],
    "MOBO": [
        "motherboard", "mainboard", "aura", "rgb fusion", "mystic light",
        "polychrome", "aorus", "z490", "z590", "z690", "z790", "b550", "x570",
        "it5702", "it8297",
    ],
    "GPU": [
        "geforce", "rtx ", "gtx ", "radeon", "rx ", "nvidia", "gpu",
        "graphic", "eagle", "gaming oc", "tuf-rtx", "rog-strix-rtx",
    ],
    "COOLER": [
        "cooler", "pump", "aio", "liquid", "kraken", "h100", "h115",
        "h150", "thermalright", "frozen", "corsair icue", "commander",
    ],
    "PERIPHERAL": [
        "keyboard", "mouse", "g213", "g502", "logitech", "razer", "headset",
    ],
}


def _classify_device(name: str, openrgb_type: object = None) -> str:
    if openrgb_type is not None:
        tname = getattr(openrgb_type, "name", str(openrgb_type)).upper()
        if tname in _OPENRGB_TYPE_MAP:
            return _OPENRGB_TYPE_MAP[tname]

    low = name.lower()
    for dtype, keywords in _TYPE_KEYWORDS.items():
        if any(k in low for k in keywords):
            return dtype
    return "OTHER"


# ---------------------------------------------------------------------------
# Riferimenti connettori ventole scheda madre
# ---------------------------------------------------------------------------

_SUPERIO_FAN_HINTS: dict[str, str] = {
    "fan #1": "Header #1 (es. CPU_FAN)",
    "fan #2": "Header #2 (es. SYS_FAN1)",
    "fan #3": "Header #3 (es. SYS_FAN2)",
    "fan #4": "Header #4 (es. SYS_FAN3)",
    "fan #5": "Header #5 (es. CPU_OPT / SYS_FAN4)",
    "fan #6": "Header #6 (es. PUMP / SYS_FAN5)",
    "fan #7": "Header #7 (es. SYS_FAN6)",
}


def _fan_reference(name: str, hardware: str, sensor_id: str) -> str:
    nl = name.lower().strip()
    if nl in _SUPERIO_FAN_HINTS:
        return f"{_SUPERIO_FAN_HINTS[nl]} • {hardware}"
    if "gpu" in nl or "nvidia" in hardware.lower() or "radeon" in hardware.lower():
        return f"Scheda Video • {hardware}"
    return hardware or sensor_id


# ---------------------------------------------------------------------------
# Rilevamento RGB via driver nativi HID
# ---------------------------------------------------------------------------

def _detect_rgb_devices(rgb_controller=None) -> list[RGBDevice]:
    try:
        if rgb_controller is None:
            from core.rgb_controller import RGBController
            rgb_controller = RGBController()
            rgb_controller.connect()

        devices: list[RGBDevice] = []
        for dev in rgb_controller.native_devices:
            zones_info = [
                RGBZoneInfo(
                    index=z["index"],
                    name=z["name"],
                    led_count=z["leds"],
                    resizable=bool(z.get("resizable", False)),
                )
                for z in dev.get("zones", [])
            ]
            devices.append(RGBDevice(
                id=dev["id"],
                name=dev["name"],
                type=dev["type"],
                zone_count=len(zones_info),
                led_count=sum(z.led_count for z in zones_info),
                zones=zones_info,
            ))
        return devices
    except Exception as exc:
        logger.warning("Rilevamento RGB nativo fallito: %r", exc)
        return []


# ---------------------------------------------------------------------------
# Rilevamento AIO LCD USB
# ---------------------------------------------------------------------------

_KNOWN_COOLER_USB = {
    ("0416", "5302"): "Thermalright AIO LCD (0416:5302)",
    ("0416", "5406"): "Thermalright AIO LCD (0416:5406)",
    ("0418", "5303"): "Thermalright AIO LCD (0418:5303)",
    ("1E71", "3008"): "NZXT Kraken LCD",
    ("1B1C", "0C1A"): "Corsair H100i/H150i AIO",
}


def _detect_usb_coolers() -> list[CoolerDevice]:
    coolers: list[CoolerDevice] = []
    seen: set[str] = set()
    try:
        import wmi as wmilib
        c = wmilib.WMI(namespace="root/cimv2")
        for dev in c.Win32_PnPEntity():
            pid_str = (dev.PNPDeviceID or "").upper()
            if "VID_" not in pid_str or "PID_" not in pid_str:
                continue
            for (vid, pid), label in _KNOWN_COOLER_USB.items():
                token = f"VID_{vid}&PID_{pid}"
                if token in pid_str and token not in seen:
                    seen.add(token)
                    coolers.append(CoolerDevice(
                        name=label,
                        pnp_id=dev.PNPDeviceID,
                        vid=vid,
                        pid=pid,
                        connected=True,
                    ))
    except Exception as exc:
        logger.debug("Rilevamento USB cooler fallito: %s", exc)
    return coolers


# ---------------------------------------------------------------------------
# Rilevamento sensori via SensorBridge + fallback nvidia-smi / psutil
# ---------------------------------------------------------------------------

def _detect_nvidia_smi() -> tuple[list[FanSensor], list[TempSensor], Optional[float]]:
    fans: list[FanSensor] = []
    temps: list[TempSensor] = []
    gpu_load: Optional[float] = None
    try:
        out = subprocess.check_output(
            [
                "nvidia-smi",
                "--query-gpu=name,temperature.gpu,fan.speed,utilization.gpu",
                "--format=csv,noheader,nounits",
            ],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=3,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        for idx, line in enumerate(out.strip().splitlines()):
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 4:
                continue
            name, temp_s, fan_s, util_s = parts[:4]
            try:
                temps.append(TempSensor(
                    id=f"nvml_temp_{idx}",
                    name="GPU Core",
                    celsius=float(temp_s),
                    hardware=name,
                    source="nvidia-smi",
                ))
            except ValueError:
                pass
            try:
                pct = int(float(fan_s))
                fans.append(FanSensor(
                    id=f"nvml_fan_{idx}",
                    name=f"{name} Fan",
                    hardware=name,
                    reference=f"Scheda Video • {name}",
                    source="nvidia-smi",
                    rpm=0,
                    duty_percent=pct,
                    controllable=False,
                ))
            except ValueError:
                pass
            try:
                gpu_load = float(util_s)
            except ValueError:
                pass
    except Exception:
        pass
    return fans, temps, gpu_load


def _read_sensors_from_bridge(bridge: SensorBridgeClient) -> tuple[list[FanSensor], list[TempSensor], dict[str, float]]:
    data = bridge.get_snapshot()
    fans: list[FanSensor] = []
    temps: list[TempSensor] = []
    loads: dict[str, float] = {
        "cpu": psutil.cpu_percent(interval=None),
        "ram": psutil.virtual_memory().percent,
        "gpu": 0.0,
    }

    for f in data.get("fans", []):
        fname = f["name"]
        hw = f.get("hardware", "")
        fans.append(FanSensor(
            id=f["id"],
            name=fname,
            hardware=hw,
            reference=_fan_reference(fname, hw, f["id"]),
            source="SensorBridge",
            rpm=int(f.get("rpm", 0)),
            duty_percent=int(f.get("duty", 0)),
            control_id=f.get("control_id", ""),
            controllable=bool(f.get("controllable", False)),
        ))

    for t in data.get("temps", []):
        temps.append(TempSensor(
            id=t["id"],
            name=t["name"],
            hardware=t.get("hardware", ""),
            celsius=float(t.get("celsius", 0.0)),
            source="SensorBridge",
        ))

    for l in data.get("loads", []):
        lname = l.get("name", "")
        if lname == "CPU Total":
            loads["cpu"] = float(l.get("percent", loads["cpu"]))
        elif lname == "GPU Core":
            loads["gpu"] = float(l.get("percent", 0.0))

    if not any(t.hardware.lower().startswith("nvidia") for t in temps):
        nv_fans, nv_temps, nv_load = _detect_nvidia_smi()
        if not fans:
            fans.extend(nv_fans)
        temps.extend(nv_temps)
        if nv_load is not None:
            loads["gpu"] = nv_load

    return fans, temps, loads


class HardwareDetector:
    """Singleton che aggiorna snapshot hardware in background."""

    _instance: Optional["HardwareDetector"] = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        self.snapshot = HardwareSnapshot()
        self._lock = threading.Lock()
        self._bridge = SensorBridgeClient()

    def detect_all(self, openrgb_client=None) -> HardwareSnapshot:
        rgb = _detect_rgb_devices(openrgb_client)
        coolers = _detect_usb_coolers()
        fans, temps, loads = _read_sensors_from_bridge(self._bridge)

        with self._lock:
            self.snapshot = HardwareSnapshot(
                rgb_devices=rgb,
                fans=fans,
                temps=temps,
                loads=loads,
                coolers=coolers,
            )
        logger.info(
            "Rilevati %d RGB, %d ventole, %d sensori temp, %d cooler LCD",
            len(rgb), len(fans), len(temps), len(coolers)
        )
        return self.snapshot

    def refresh_sensors(self) -> None:
        fans, temps, loads = _read_sensors_from_bridge(self._bridge)
        with self._lock:
            self.snapshot.fans = fans
            self.snapshot.temps = temps
            self.snapshot.loads = loads

    def get_snapshot(self) -> HardwareSnapshot:
        with self._lock:
            return self.snapshot
