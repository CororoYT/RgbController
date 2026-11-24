"""
widgets/gauge.py – Gauge circolare animata per RPM e temperature.
"""
import customtkinter as ctk
import tkinter as tk
import math


class GaugeWidget(ctk.CTkFrame):
    """
    Gauge ad arco semicircolare con:
    - Valore corrente e unità
    - Colore che cambia da verde → giallo → rosso in base alla soglia
    """

    def __init__(self, master, label: str = "", unit: str = "",
                 min_val: float = 0, max_val: float = 100,
                 warn_pct: float = 0.70, crit_pct: float = 0.90,
                 size: int = 130, **kwargs):
        super().__init__(master, **kwargs)
        self.label    = label
        self.unit     = unit
        self.min_val  = min_val
        self.max_val  = max_val
        self.warn_pct = warn_pct
        self.crit_pct = crit_pct
        self.size     = size
        self._value   = min_val
        self._build()

    def _build(self):
        self._canvas = tk.Canvas(
            self, width=self.size, height=self.size * 0.75,
            bg="#1e1e2e", highlightthickness=0
        )
        self._canvas.pack(pady=(4, 0))
        ctk.CTkLabel(self, text=self.label, font=ctk.CTkFont(size=11)).pack()
        self._render_gauge(self._value)

    def _arc_color(self, pct: float) -> str:
        if pct >= self.crit_pct:
            return "#FF4444"
        if pct >= self.warn_pct:
            return "#FFA500"
        return "#00CC66"

    def _render_gauge(self, value: float):
        self._canvas.delete("all")
        cx = self.size / 2
        cy = self.size * 0.72
        r  = self.size * 0.42
        pad= self.size * 0.08

        # sfondo arco grigio
        self._canvas.create_arc(
            cx - r, cy - r, cx + r, cy + r,
            start=0, extent=180, style="arc",
            outline="#333", width=10
        )

        # arco valore
        pct   = max(0.0, min(1.0, (value - self.min_val) / (self.max_val - self.min_val)))
        color = self._arc_color(pct)
        extent= pct * 180
        self._canvas.create_arc(
            cx - r, cy - r, cx + r, cy + r,
            start=180 - extent, extent=extent, style="arc",
            outline=color, width=10
        )

        # testo valore
        self._canvas.create_text(
            cx, cy - r * 0.3,
            text=f"{value:.0f}",
            font=("Segoe UI", int(self.size * 0.18), "bold"),
            fill="white"
        )
        self._canvas.create_text(
            cx, cy - r * 0.3 + self.size * 0.14,
            text=self.unit,
            font=("Segoe UI", int(self.size * 0.10)),
            fill="#aaa"
        )

    def set_value(self, value: float):
        self._value = value
        self._render_gauge(value)
