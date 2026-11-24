"""
pages/rgb_page.py – Pagina controllo RGB LED con supporto completo alle Zone / Header ARGB (D_LED1, D_LED2).
"""
from __future__ import annotations
import customtkinter as ctk
from core.app_controller import AppController
from core.hardware_detector import RGBDevice
from gui.widgets.color_picker import ColorPickerWidget
from core.rgb_controller import EFFECTS


class RGBPage(ctk.CTkFrame):
    def __init__(self, master, controller: AppController, **kwargs):
        super().__init__(master, **kwargs)
        self.ctrl = controller
        self._selected_device: RGBDevice | None = None
        self._device_frames: dict[int, ctk.CTkButton] = {}
        self._build()
        self._load_devices()

    def _build(self):
        self.grid_columnconfigure(0, weight=0, minsize=240)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self._sidebar = ctk.CTkScrollableFrame(self, width=230, label_text="🖥 Dispositivi RGB")
        self._sidebar.grid(row=0, column=0, sticky="nsew", padx=(8, 4), pady=8)

        self._btn_all = ctk.CTkButton(
            self._sidebar, text="⚡ Applica a TUTTI",
            command=self._apply_all, fg_color="#6366f1", hover_color="#4f46e5"
        )
        self._btn_all.pack(fill="x", pady=(4, 4))

        self._btn_reload = ctk.CTkButton(
            self._sidebar, text="🔄 Ricarica Luci",
            command=self._reload_lights, fg_color="#334155", hover_color="#475569"
        )
        self._btn_reload.pack(fill="x", pady=(0, 8))

        self._editor = ctk.CTkFrame(self)
        self._editor.grid(row=0, column=1, sticky="nsew", padx=(4, 8), pady=8)
        self._build_editor()

    def _build_editor(self):
        self._no_sel_label = ctk.CTkLabel(
            self._editor,
            text="← Seleziona un dispositivo\ndall'elenco a sinistra",
            font=ctk.CTkFont(size=14),
            text_color="#666",
        )
        self._no_sel_label.place(relx=0.5, rely=0.5, anchor="center")
        self._editor_inner = ctk.CTkScrollableFrame(self._editor, fg_color="transparent")

    def _build_device_editor(self, dev: RGBDevice):
        for w in self._editor_inner.winfo_children():
            w.destroy()
        self._no_sel_label.place_forget()
        self._editor_inner.place(relx=0, rely=0, relwidth=1, relheight=1)

        cfg = self.ctrl.settings.get_device_settings(dev.id)

        icon = {
            "RAM": "💾", "GPU": "🎮", "MOBO": "🔧",
            "COOLER": "💧", "PERIPHERAL": "⌨"
        }.get(dev.type, "💡")

        ctk.CTkLabel(
            self._editor_inner,
            text=f"{icon}  {dev.name}",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).pack(pady=(12, 2), padx=16, anchor="w")

        ctk.CTkLabel(
            self._editor_inner,
            text=f"Categoria: {dev.type}  |  Zone: {dev.zone_count}  |  LED totali: {dev.led_count}",
            font=ctk.CTkFont(size=11),
            text_color="#888",
        ).pack(padx=16, anchor="w")

        ctk.CTkFrame(self._editor_inner, height=2, fg_color="#333").pack(fill="x", padx=16, pady=8)

        row = ctk.CTkFrame(self._editor_inner, fg_color="transparent")
        row.pack(fill="x", padx=16, pady=4)
        row.columnconfigure(0, weight=0)
        row.columnconfigure(1, weight=1)

        r, g, b = cfg["color"]
        self._picker = ColorPickerWidget(row, on_color_change=self._on_color_live)
        self._picker.set_color(r, g, b)
        self._picker.grid(row=0, column=0, padx=(0, 16), sticky="n")

        right = ctk.CTkFrame(row, fg_color="transparent")
        right.grid(row=0, column=1, sticky="nsew")

        ctk.CTkLabel(right, text="Effetto", font=ctk.CTkFont(size=12, weight="bold")).pack(anchor="w")
        self._effect_var = ctk.StringVar(value=cfg["effect"])
        ctk.CTkOptionMenu(
            right, variable=self._effect_var,
            values=EFFECTS, width=200
        ).pack(anchor="w", pady=(2, 12))

        ctk.CTkLabel(right, text="Luminosità", font=ctk.CTkFont(size=12, weight="bold")).pack(anchor="w")
        self._bright_var = ctk.IntVar(value=cfg["brightness"])
        bright_row = ctk.CTkFrame(right, fg_color="transparent")
        bright_row.pack(fill="x", pady=(2, 12))
        ctk.CTkSlider(
            bright_row, from_=0, to=100,
            variable=self._bright_var, command=self._on_bright_change
        ).pack(side="left", fill="x", expand=True)
        self._bright_label = ctk.CTkLabel(bright_row, text=f"{cfg['brightness']}%", width=38)
        self._bright_label.pack(side="left", padx=4)

        ctk.CTkLabel(right, text="Colori vividi rapidi", font=ctk.CTkFont(size=12, weight="bold")).pack(anchor="w", pady=(4, 4))
        quick = ctk.CTkFrame(right, fg_color="transparent")
        quick.pack(anchor="w", pady=(0, 8))
        palette = [
            ("#FF0000", (255, 0, 0)),       # Rosso puro
            ("#FF5500", (255, 85, 0)),      # Arancione
            ("#FFCC00", (255, 204, 0)),     # Oro
            ("#00FF33", (0, 255, 51)),      # Verde neon
            ("#00FFCC", (0, 255, 204)),     # Menta
            ("#00B4FF", (0, 180, 255)),     # Ciano
            ("#0033FF", (0, 51, 255)),      # Blu elettrico
            ("#7C00FF", (124, 0, 255)),     # Viola
            ("#FF00CC", (255, 0, 204)),     # Magenta
            ("#FFFFFF", (255, 255, 255)),   # Bianco
            ("#111111", (0, 0, 0)),         # Spento
        ]
        for hex_c, rgb_t in palette:
            ctk.CTkButton(
                quick, text="", width=24, height=24, corner_radius=12,
                fg_color=hex_c, hover_color=hex_c,
                command=lambda c=rgb_t: self._picker.set_color(*c)
            ).pack(side="left", padx=2)

        self._sync_all_var = ctk.BooleanVar(value=True)
        ctk.CTkSwitch(
            right, text="🔗 Sincronizza tutti (RAM + Ventole + Mobo + Tastiera)",
            variable=self._sync_all_var, font=ctk.CTkFont(size=12)
        ).pack(anchor="w", pady=(6, 8))

        ctk.CTkButton(
            right, text="✅ Applica Colore", command=self._apply_current,
            fg_color="#16a34a", hover_color="#15803d", height=40,
        ).pack(anchor="w", pady=(4, 0))

        # ---- Sezione Zone / Header ARGB (D_LED1, D_LED2 per ventole Cooler Master / Thermalright) ----
        if dev.zones:
            ctk.CTkFrame(self._editor_inner, height=2, fg_color="#333").pack(fill="x", padx=16, pady=12)
            ctk.CTkLabel(
                self._editor_inner,
                text="🔌 Zone & Header ARGB (Ventole Case / AIO collegate alla Scheda Madre)",
                font=ctk.CTkFont(size=13, weight="bold"),
                text_color="#38bdf8",
            ).pack(anchor="w", padx=16, pady=(0, 6))

            for z in dev.zones:
                zrow = ctk.CTkFrame(self._editor_inner, fg_color="#1e1e2e")
                zrow.pack(fill="x", padx=16, pady=3)

                hint = ""
                if "D_LED" in z.name.upper():
                    hint = "  (Header ARGB 5V Ventole/AIO)"
                ctk.CTkLabel(
                    zrow, text=f"{z.name}{hint}", width=250, anchor="w",
                    font=ctk.CTkFont(size=12, weight="bold" if hint else "normal")
                ).pack(side="left", padx=10, pady=8)

                if z.resizable:
                    led_var = ctk.StringVar(value=str(z.led_count))
                    ctk.CTkLabel(zrow, text="LED:", font=ctk.CTkFont(size=11), text_color="#94a3b8").pack(side="left")
                    ctk.CTkEntry(zrow, textvariable=led_var, width=55).pack(side="left", padx=4)
                    ctk.CTkButton(
                        zrow, text="Salva LED", width=80, height=28,
                        fg_color="#334155", hover_color="#475569",
                        command=lambda idx=z.index, v=led_var, d=dev: self._resize_zone(d, idx, v)
                    ).pack(side="left", padx=4)
                else:
                    ctk.CTkLabel(
                        zrow, text=f"{z.led_count} LED", width=120,
                        font=ctk.CTkFont(size=11), text_color="#94a3b8"
                    ).pack(side="left", padx=4)

                ctk.CTkButton(
                    zrow, text="🎨 Applica colore a questa zona", width=190, height=28,
                    fg_color="#4f46e5", hover_color="#4338ca",
                    command=lambda idx=z.index, d=dev: self._apply_zone(d, idx)
                ).pack(side="right", padx=10)

    def _resize_zone(self, dev: RGBDevice, zone_idx: int, led_var: ctk.StringVar):
        try:
            count = max(0, min(200, int(led_var.get().strip())))
        except ValueError:
            return
        if self.ctrl.resize_zone(dev.id, zone_idx, count):
            self._build_device_editor(dev)

    def _apply_zone(self, dev: RGBDevice, zone_idx: int):
        r, g, b = self._picker.get_color()
        brightness = int(self._bright_var.get())
        self.ctrl.apply_zone_rgb(dev.id, zone_idx, r, g, b, brightness)

    def _load_devices(self):
        for w in self._sidebar.winfo_children():
            if w not in (self._btn_all, self._btn_reload):
                w.destroy()
        self._device_frames.clear()

        snap = self.ctrl.detector.get_snapshot()
        grouped: dict[str, list[RGBDevice]] = {}
        for dev in snap.rgb_devices:
            grouped.setdefault(dev.type, []).append(dev)

        type_labels = {
            "MOBO": ("🔧", "Scheda Madre & Header ARGB"),
            "GPU": ("🎮", "Scheda Video"),
            "RAM": ("💾", "RAM"),
            "COOLER": ("💧", "Raffreddamento"),
            "PERIPHERAL": ("⌨", "Periferiche"),
            "OTHER": ("💡", "Altro"),
        }

        for dtype in ["MOBO", "GPU", "RAM", "COOLER", "PERIPHERAL", "OTHER"]:
            devs = grouped.get(dtype, [])
            if not devs:
                continue
            icon, title = type_labels[dtype]
            ctk.CTkLabel(
                self._sidebar,
                text=f"{icon} {title}",
                font=ctk.CTkFont(size=11, weight="bold"),
                text_color="#888",
            ).pack(anchor="w", padx=4, pady=(8, 2))

            for dev in devs:
                btn = ctk.CTkButton(
                    self._sidebar,
                    text=dev.name,
                    anchor="w",
                    command=lambda d=dev: self._select_device(d),
                    fg_color="#2d2d3f",
                    hover_color="#3d3d5f",
                    text_color="white",
                    height=34,
                )
                btn.pack(fill="x", pady=1)
                self._device_frames[dev.id] = btn

        if not snap.rgb_devices:
            ctk.CTkLabel(
                self._sidebar,
                text="⚠ Nessun dispositivo RGB\nrilevato.\n\nAvvia l'app come\nAmministratore per\nrilevare anche RAM e GPU.",
                font=ctk.CTkFont(size=11),
                text_color="#888",
                justify="left",
            ).pack(padx=8, pady=16)

    def _select_device(self, dev: RGBDevice):
        self._selected_device = dev
        for did, btn in self._device_frames.items():
            btn.configure(fg_color="#4f46e5" if did == dev.id else "#2d2d3f")
        self._build_device_editor(dev)

    def _on_color_live(self, r: int, g: int, b: int):
        pass

    def _on_bright_change(self, val):
        self._bright_label.configure(text=f"{int(float(val))}%")

    def _apply_current(self):
        if self._selected_device is None:
            return
        r, g, b = self._picker.get_color()
        brightness = int(self._bright_var.get())
        effect = self._effect_var.get()
        if hasattr(self, "_sync_all_var") and self._sync_all_var.get():
            self.ctrl.apply_rgb_all(r, g, b, brightness, effect)
        else:
            self.ctrl.apply_rgb(self._selected_device.id, r, g, b, brightness, effect)

    def _apply_all(self):
        if not hasattr(self, "_picker"):
            return
        r, g, b = self._picker.get_color()
        brightness = int(self._bright_var.get()) if hasattr(self, "_bright_var") else 100
        effect = self._effect_var.get() if hasattr(self, "_effect_var") else "Static"
        self.ctrl.apply_rgb_all(r, g, b, brightness, effect)

    def _reload_lights(self):
        self.ctrl.reload_rgb()
        self._load_devices()
        if self._selected_device is not None:
            snap = self.ctrl.detector.get_snapshot()
            updated = next((d for d in snap.rgb_devices if d.id == self._selected_device.id), None)
            if updated is not None:
                self._select_device(updated)

    def refresh(self):
        self._load_devices()
