"""
fan_controller.py – Controllo diretto delle ventole tramite il nostro SensorBridge nativo.
"""
from __future__ import annotations
import logging
from core.sensor_bridge import SensorBridgeClient

logger = logging.getLogger(__name__)


class FanController:
    """Controlla le ventole tramite SensorBridge (LibreHardwareMonitorLib in-process)."""

    def __init__(self):
        self._bridge = SensorBridgeClient()

    @property
    def is_available(self) -> bool:
        return self._bridge.is_available

    def set_fan_duty(self, control_id: str, duty: float) -> bool:
        """Imposta il duty cycle (0-100) di un controllo ventola."""
        if not control_id:
            logger.warning("set_fan_duty: control_id vuoto (ventola non controllabile)")
            return False
        ok = self._bridge.set_fan_duty(control_id, duty)
        if ok:
            logger.info("Ventola %s -> %.0f%%", control_id, duty)
        else:
            logger.warning("set_fan_duty fallito su %s", control_id)
        return ok

    def set_fan_auto(self, control_id: str) -> bool:
        """Rimette la ventola in modalità automatica (gestita dal BIOS/VBIOS)."""
        if not control_id:
            return False
        ok = self._bridge.set_fan_auto(control_id)
        if ok:
            logger.info("Ventola %s -> AUTO", control_id)
        return ok
