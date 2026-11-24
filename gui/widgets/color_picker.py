"""
widgets/color_picker.py – Selettore colore HSV ad alta saturazione e preview live.
"""
import customtkinter as ctk
import tkinter as tk
import colorsys
import math


class ColorPickerWidget(ctk.CTkFrame):
    """Ruota colori HSV pulita + preview + input HEX (senza slider luminosità duplicato)."""

    WHEEL_SIZE = 196

    def __init__(self, master, on_color_change=None, **kwargs):
        super().__init__(master, **kwargs)
        self.on_color_change = on_color_change
        self._r, self._g, self._b = 0, 180, 255
        self._hue = 0.55
        self._sat = 1.0
        self._val = 1.0
        self._build()

    def _build(self):
        self._canvas = tk.Canvas(
            self, width=self.WHEEL_SIZE, height=self.WHEEL_SIZE,
            bg="#1e1e2e", highlightthickness=0,
        )
        self._canvas.grid(row=0, column=0, columnspan=2, pady=(10, 6), padx=10)
        self._draw_wheel()
        self._canvas.bind("<Button-1>", self._on_wheel_click)
        self._canvas.bind("<B1-Motion>", self._on_wheel_click)

        self._preview = tk.Canvas(
            self, width=54, height=34, bg="#00B4FF",
            highlightthickness=2, highlightbackground="#475569"
        )
        self._preview.grid(row=1, column=0, padx=(10, 4), pady=(4, 10), sticky="w")

        self._hex_var = ctk.StringVar(value="#00B4FF")
        self._hex_entry = ctk.CTkEntry(
            self, textvariable=self._hex_var, width=105, height=34,
            font=ctk.CTkFont(family="Consolas", size=13, weight="bold"),
        )
        self._hex_entry.grid(row=1, column=1, padx=(4, 10), pady=(4, 10), sticky="e")
        self._hex_entry.bind("<Return>", self._on_hex_enter)

        self.columnconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)

    def _draw_wheel(self):
        cx = cy = self.WHEEL_SIZE // 2
        max_r = cx - 6
        # Disegna anelli concentrici per sfumatura fluida dal bianco al centro ai colori saturi sul bordo
        steps = 6
        for ring in range(steps):
            r_outer = max_r * (steps - ring) / steps
            r_inner = max_r * (steps - ring - 1) / steps
            sat = (steps - ring) / steps
            for angle_deg in range(0, 360, 2):
                a = math.radians(angle_deg)
                rv, gv, bv = colorsys.hsv_to_rgb(angle_deg / 360.0, sat, 1.0)
                col = "#{:02x}{:02x}{:02x}".format(int(rv * 255), int(gv * 255), int(bv * 255))
                x1 = cx + r_inner * math.cos(a)
                y1 = cy + r_inner * math.sin(a)
                x2 = cx + r_outer * math.cos(a)
                y2 = cy + r_outer * math.sin(a)
                self._canvas.create_line(x1, y1, x2, y2, fill=col, width=4)

        self._canvas.create_oval(cx - 10, cy - 10, cx + 10, cy + 10, fill="#ffffff", outline="#cbd5e1")
        self._cursor = self._canvas.create_oval(
            cx - 7, cy - 7, cx + 7, cy + 7, outline="#000000", width=3, fill=""
        )
        self._cursor_inner = self._canvas.create_oval(
            cx - 5, cy - 5, cx + 5, cy + 5, outline="#ffffff", width=2, fill=""
        )

    def _move_cursor_to(self, x: float, y: float):
        self._canvas.coords(self._cursor, x - 7, y - 7, x + 7, y + 7)
        self._canvas.coords(self._cursor_inner, x - 5, y - 5, x + 5, y + 5)

    def _on_wheel_click(self, event):
        cx = cy = self.WHEEL_SIZE // 2
        dx, dy = event.x - cx, event.y - cy
        dist = math.hypot(dx, dy)
        max_r = cx - 6
        if dist > max_r:
            dx = dx * max_r / dist
            dy = dy * max_r / dist
            dist = max_r

        self._hue = (math.degrees(math.atan2(dy, dx)) % 360) / 360.0
        # Se clicchi sul bordo esterno (oltre l'80% del raggio), forza saturazione 100% per colori vividi
        self._sat = 1.0 if dist >= max_r * 0.80 else min(dist / max_r, 1.0)
        self._val = 1.0
        self._move_cursor_to(cx + dx, cy + dy)

        rv, gv, bv = colorsys.hsv_to_rgb(self._hue, self._sat, self._val)
        self._r, self._g, self._b = int(round(rv * 255)), int(round(gv * 255)), int(round(bv * 255))
        self._sync_ui()

    def _on_hex_enter(self, _event=None):
        try:
            h = self._hex_var.get().strip().lstrip("#")
            if len(h) == 6:
                self._r = int(h[0:2], 16)
                self._g = int(h[2:4], 16)
                self._b = int(h[4:6], 16)
                self._sync_ui(update_hex=False)
        except ValueError:
            pass

    def _sync_ui(self, update_hex=True):
        col = "#{:02x}{:02x}{:02x}".format(self._r, self._g, self._b)
        self._preview.config(bg=col)
        if update_hex:
            self._hex_var.set(col.upper())
        if self.on_color_change:
            self.on_color_change(self._r, self._g, self._b)

    def get_color(self) -> tuple[int, int, int]:
        return self._r, self._g, self._b

    def set_color(self, r: int, g: int, b: int):
        self._r, self._g, self._b = r, g, b
        h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
        self._hue, self._sat, self._val = h, s, v
        cx = cy = self.WHEEL_SIZE // 2
        max_r = cx - 6
        angle = h * 2 * math.pi
        self._move_cursor_to(cx + s * max_r * math.cos(angle), cy + s * max_r * math.sin(angle))
        self._sync_ui()
