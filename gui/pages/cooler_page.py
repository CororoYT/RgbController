"""
pages/cooler_page.py – Monitor Raffreddamento a Liquido & Display LCD AIO (alternativa user-friendly a TRCC).
"""
from __future__ import annotations
import datetime
import os
import tkinter as tk
from tkinter import filedialog
import customtkinter as ctk
from PIL import Image, ImageTk

from core.app_controller import AppController
from core.hardware_detector import HardwareSnapshot


LCD_PRESETS = {
    "Cyberpunk": {
        "bg_mode": "gradient",
        "bg_color": "#0f172a",
        "accent_color": "#38bdf8",
        "text_color": "#f8fafc",
    },
    "Minimal Dark": {
        "bg_mode": "solid",
        "bg_color": "#09090b",
        "accent_color": "#a855f7",
        "text_color": "#fafafa",
    },
    "Ice Blue": {
        "bg_mode": "gradient",
        "bg_color": "#082f49",
        "accent_color": "#22d3ee",
        "text_color": "#ecfeff",
    },
    "Thermal Alert": {
        "bg_mode": "solid",
        "bg_color": "#1c0a0a",
        "accent_color": "#ef4444",
        "text_color": "#fef2f2",
    },
}


class CoolerPage(ctk.CTkFrame):
    """Dashboard intuitiva per il raffreddamento a liquido e l'anteprima LCD AIO."""

    PREVIEW_SIZE = 280

    def __init__(self, master, controller: AppController, **kwargs):
        super().__init__(master, **kwargs)
        self.ctrl = controller
        self._cfg = self.ctrl.settings.get_lcd_config()
        self._bg_photo = None
        self._cached_img_key: tuple[str, int] | None = None
        self._last_snapshot: HardwareSnapshot = self.ctrl.detector.get_snapshot()

        self._build()
        self.update_snapshot(self._last_snapshot)

    def _build(self):
        self.grid_columnconfigure(0, weight=0, minsize=340)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # ---- Header ----
        hdr = ctk.CTkFrame(self, fg_color="transparent")
        hdr.grid(row=0, column=0, columnspan=2, sticky="ew", padx=16, pady=(12, 4))
        ctk.CTkLabel(
            hdr, text="💧 Raffreddamento a Liquido & Monitor LCD",
            font=ctk.CTkFont(size=18, weight="bold")
        ).pack(side="left")

        self._usb_badge = ctk.CTkLabel(
            hdr, text="⚪ Ricerca AIO USB...",
            font=ctk.CTkFont(size=12), text_color="#94a3b8"
        )
        self._usb_badge.pack(side="right", padx=8)

        # ---- Colonna sinistra: Anteprima Live LCD + Telemetria AIO ----
        left = ctk.CTkFrame(self, fg_color="#1e1e2e")
        left.grid(row=1, column=0, sticky="nsew", padx=(16, 8), pady=8)

        ctk.CTkLabel(
            left, text="Anteprima Display AIO",
            font=ctk.CTkFont(size=13, weight="bold"), text_color="#cbd5e1"
        ).pack(pady=(12, 6))

        self._canvas = tk.Canvas(
            left, width=self.PREVIEW_SIZE, height=self.PREVIEW_SIZE,
            bg="#1e1e2e", highlightthickness=0
        )
        self._canvas.pack(pady=6)

        # Card Telemetria rapida
        tele = ctk.CTkFrame(left, fg_color="#141422")
        tele.pack(fill="x", padx=14, pady=10)

        self._lbl_cpu_t = self._stat_row(tele, "🌡 CPU Temp:", "-- °C")
        self._lbl_gpu_t = self._stat_row(tele, "🎮 GPU Temp:", "-- °C")
        self._lbl_load = self._stat_row(tele, "⚡ Carico CPU / RAM:", "--% / --%")
        self._lbl_pump = self._stat_row(tele, "🌀 Ventole / Pompa:", "-- RPM")

        # ---- Colonna destra: Controlli semplici (stile TRCC semplificato) ----
        right = ctk.CTkScrollableFrame(self, fg_color="#1e1e2e")
        right.grid(row=1, column=1, sticky="nsew", padx=(8, 16), pady=8)

        self._section(right, "🎨 Tema Rapido")
        self._preset_var = ctk.StringVar(value=self._cfg.get("preset", "Cyberpunk"))
        ctk.CTkSegmentedButton(
            right,
            values=list(LCD_PRESETS.keys()),
            variable=self._preset_var,
            command=self._apply_preset,
        ).pack(fill="x", padx=12, pady=4)

        self._section(right, "📐 Forma & Risoluzione Schermo")
        shape_row = ctk.CTkFrame(right, fg_color="transparent")
        shape_row.pack(fill="x", padx=12, pady=4)
        self._shape_var = ctk.StringVar(value=self._cfg.get("shape", "circle"))
        ctk.CTkSegmentedButton(
            shape_row, values=["circle", "square"],
            variable=self._shape_var, command=lambda _: self._save_and_redraw()
        ).pack(side="left")

        self._res_var = ctk.StringVar(value=self._cfg.get("resolution", "320x320"))
        ctk.CTkOptionMenu(
            shape_row, variable=self._res_var,
            values=["240x240", "240x320", "320x320", "480x480"],
            command=lambda _: self._save_and_redraw(), width=120
        ).pack(side="left", padx=12)

        self._section(right, "🖼 Sfondo")
        bg_row = ctk.CTkFrame(right, fg_color="transparent")
        bg_row.pack(fill="x", padx=12, pady=4)
        self._bg_mode_var = ctk.StringVar(value=self._cfg.get("bg_mode", "gradient"))
        ctk.CTkSegmentedButton(
            bg_row, values=["solid", "gradient", "image"],
            variable=self._bg_mode_var, command=lambda _: self._save_and_redraw()
        ).pack(side="left")
        ctk.CTkButton(
            bg_row, text="📁 Scegli Immagine/GIF", width=150,
            command=self._pick_image
        ).pack(side="left", padx=12)

        self._section(right, "📊 Dati mostrati sul display")
        self._chk_cpu_t = self._checkbox(right, "Temperatura CPU", "show_cpu_temp")
        self._chk_gpu_t = self._checkbox(right, "Temperatura GPU", "show_gpu_temp")
        self._chk_load = self._checkbox(right, "Carico CPU (%)", "show_cpu_load")
        self._chk_rpm = self._checkbox(right, "Velocità Ventole / Pompa (RPM)", "show_fan_rpm")
        self._chk_clock = self._checkbox(right, "Orologio digitale", "show_clock")

        self._section(right, "✏ Etichetta personalizzata")
        lbl_row = ctk.CTkFrame(right, fg_color="transparent")
        lbl_row.pack(fill="x", padx=12, pady=4)
        self._label_var = ctk.StringVar(value=self._cfg.get("custom_label", "LIQUID COOLING"))
        ctk.CTkEntry(lbl_row, textvariable=self._label_var, width=240).pack(side="left")
        ctk.CTkButton(
            lbl_row, text="💾 Salva", width=90,
            fg_color="#16a34a", hover_color="#15803d",
            command=self._save_label
        ).pack(side="left", padx=8)

    def _stat_row(self, parent, title: str, initial: str) -> ctk.CTkLabel:
        r = ctk.CTkFrame(parent, fg_color="transparent")
        r.pack(fill="x", padx=10, pady=3)
        ctk.CTkLabel(r, text=title, font=ctk.CTkFont(size=11), text_color="#94a3b8").pack(side="left")
        val = ctk.CTkLabel(r, text=initial, font=ctk.CTkFont(size=12, weight="bold"), text_color="#f8fafc")
        val.pack(side="right")
        return val

    def _section(self, parent, title: str):
        ctk.CTkLabel(
            parent, text=title, font=ctk.CTkFont(size=13, weight="bold"),
            text_color="#38bdf8"
        ).pack(anchor="w", padx=12, pady=(14, 2))
        ctk.CTkFrame(parent, height=1, fg_color="#334155").pack(fill="x", padx=12, pady=(0, 6))

    def _checkbox(self, parent, text: str, key: str) -> ctk.CTkCheckBox:
        var = ctk.BooleanVar(value=bool(self._cfg.get(key, True)))
        cb = ctk.CTkCheckBox(
            parent, text=text, variable=var,
            command=lambda k=key, v=var: self._toggle_opt(k, v)
        )
        cb.pack(anchor="w", padx=16, pady=3)
        return cb

    def _toggle_opt(self, key: str, var: ctk.BooleanVar):
        self._cfg[key] = bool(var.get())
        self._save_and_redraw()

    def _apply_preset(self, name: str):
        preset = LCD_PRESETS.get(name)
        if not preset:
            return
        self._cfg["preset"] = name
        self._cfg.update(preset)
        self._bg_mode_var.set(preset["bg_mode"])
        self._save_and_redraw()

    def _pick_image(self):
        path = filedialog.askopenfilename(
            title="Seleziona immagine o GIF per LCD AIO",
            filetypes=[("Immagini", "*.png;*.jpg;*.jpeg;*.gif;*.bmp"), ("Tutti i file", "*.*")]
        )
        if path:
            self._cfg["image_path"] = path
            self._cfg["bg_mode"] = "image"
            self._bg_mode_var.set("image")
            self._save_and_redraw()

    def _save_label(self):
        self._cfg["custom_label"] = self._label_var.get()
        self._save_and_redraw()

    def _save_and_redraw(self):
        self._cfg["shape"] = self._shape_var.get()
        self._cfg["resolution"] = self._res_var.get()
        self._cfg["bg_mode"] = self._bg_mode_var.get()
        self.ctrl.settings.save_lcd_config(self._cfg)
        self._draw_lcd_preview()

    # ------------------------------------------------------------------ #
    # Aggiornamento telemetria live                                      #
    # ------------------------------------------------------------------ #

    def _extract_metrics(self, snap: HardwareSnapshot):
        cpu_temp = None
        gpu_temp = None
        for t in snap.temps:
            nl = t.name.lower()
            hl = t.hardware.lower()
            if cpu_temp is None and ("package" in nl or "core max" in nl or "cpu" in hl):
                cpu_temp = t.celsius
            if gpu_temp is None and ("gpu core" in nl or "nvidia" in hl or "amd" in hl):
                gpu_temp = t.celsius

        cpu_load = snap.loads.get("cpu", 0.0)
        ram_load = snap.loads.get("ram", 0.0)
        max_rpm = max((f.rpm for f in snap.fans), default=0)
        return cpu_temp, gpu_temp, cpu_load, ram_load, max_rpm

    def update_snapshot(self, snap: HardwareSnapshot):
        self._last_snapshot = snap
        if snap.coolers:
            self._usb_badge.configure(
                text=f"🟢 {snap.coolers[0].name}",
                text_color="#22c55e"
            )
        else:
            self._usb_badge.configure(
                text="🟡 Modalità Monitor Software (nessun LCD USB rilevato)",
                text_color="#eab308"
            )

        cpu_t, gpu_t, cpu_l, ram_l, max_rpm = self._extract_metrics(snap)
        self._lbl_cpu_t.configure(text=f"{cpu_t:.0f} °C" if cpu_t is not None else "N/D")
        self._lbl_gpu_t.configure(text=f"{gpu_t:.0f} °C" if gpu_t is not None else "N/D")
        self._lbl_load.configure(text=f"{cpu_l:.0f}% / {ram_l:.0f}%")
        self._lbl_pump.configure(text=f"{max_rpm} RPM" if max_rpm else "0 RPM")

        self._draw_lcd_preview()

    def _draw_lcd_preview(self):
        c = self._canvas
        c.delete("all")
        s = self.PREVIEW_SIZE
        pad = 10
        shape = self._cfg.get("shape", "circle")
        bg_col = self._cfg.get("bg_color", "#0f172a")
        accent = self._cfg.get("accent_color", "#38bdf8")
        txt_col = self._cfg.get("text_color", "#f8fafc")

        # Sfondo
        img_path = self._cfg.get("image_path", "")
        if self._cfg.get("bg_mode") == "image" and os.path.exists(img_path):
            try:
                key = (img_path, s - 2 * pad)
                if self._cached_img_key != key or self._bg_photo is None:
                    img = Image.open(img_path).convert("RGB")
                    img = img.resize((s - 2 * pad, s - 2 * pad), Image.Resampling.LANCZOS)
                    self._bg_photo = ImageTk.PhotoImage(img)
                    self._cached_img_key = key
                c.create_image(s // 2, s // 2, image=self._bg_photo)
            except Exception:
                c.create_rectangle(pad, pad, s - pad, s - pad, fill=bg_col, outline=accent, width=3)
        else:
            if shape == "circle":
                c.create_oval(pad, pad, s - pad, s - pad, fill=bg_col, outline=accent, width=4)
                if self._cfg.get("bg_mode") == "gradient":
                    c.create_arc(
                        pad + 6, pad + 6, s - pad - 6, s - pad - 6,
                        start=45, extent=220, style="arc", outline=accent, width=3
                    )
            else:
                c.create_rectangle(pad, pad, s - pad, s - pad, fill=bg_col, outline=accent, width=4)

        cpu_t, gpu_t, cpu_l, _, max_rpm = self._extract_metrics(self._last_snapshot)

        # Label superiore
        c.create_text(
            s // 2, 46,
            text=self._cfg.get("custom_label", "LIQUID COOLING")[:18],
            font=("Segoe UI", 9, "bold"), fill=accent
        )

        # Temperatura principale (CPU)
        y_main = s // 2 - 20
        if self._cfg.get("show_cpu_temp", True):
            t_txt = f"{cpu_t:.0f}°" if cpu_t is not None else "--°"
            c.create_text(s // 2, y_main, text=t_txt, font=("Segoe UI", 42, "bold"), fill=txt_col)
            c.create_text(s // 2, y_main + 34, text="CPU TEMP", font=("Segoe UI", 8), fill="#94a3b8")

        # Riga statistiche secondarie
        sub_parts = []
        if self._cfg.get("show_gpu_temp", True) and gpu_t is not None:
            sub_parts.append(f"GPU {gpu_t:.0f}°C")
        if self._cfg.get("show_cpu_load", True):
            sub_parts.append(f"LOAD {cpu_l:.0f}%")
        if self._cfg.get("show_fan_rpm", True) and max_rpm:
            sub_parts.append(f"{max_rpm} RPM")

        if sub_parts:
            c.create_text(
                s // 2, s - 68,
                text="  •  ".join(sub_parts),
                font=("Segoe UI", 9, "bold"), fill=txt_col
            )

        if self._cfg.get("show_clock", True):
            now_s = datetime.datetime.now().strftime("%H:%M:%S")
            c.create_text(s // 2, s - 40, text=now_s, font=("Consolas", 10), fill=accent)
