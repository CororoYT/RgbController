"""
pages/fan_page.py – Pagina monitoraggio e controllo ventole con riferimenti connettori e nomi personalizzabili.
"""
from __future__ import annotations
import customtkinter as ctk
from core.app_controller import AppController
from core.hardware_detector import FanSensor, TempSensor, HardwareSnapshot
from gui.widgets.gauge import GaugeWidget


class FanPage(ctk.CTkFrame):
    def __init__(self, master, controller: AppController, **kwargs):
        super().__init__(master, **kwargs)
        self.ctrl = controller
        self._fan_rows: dict[str, "_FanRow"] = {}
        self._gauge_map: dict[str, GaugeWidget] = {}
        self._build()
        self._load_snapshot(self.ctrl.detector.get_snapshot())

    def _build(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        header = ctk.CTkFrame(self, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew", padx=12, pady=(10, 4))
        ctk.CTkLabel(header, text="💨 Ventole & Temperature",
                     font=ctk.CTkFont(size=18, weight="bold")).pack(side="left")
        ctk.CTkButton(header, text="🔄 Riscansiona",
                      command=self._rescan, width=120).pack(side="right")

        self._tabs = ctk.CTkTabview(self)
        self._tabs.grid(row=1, column=0, sticky="nsew", padx=12, pady=(0, 8))
        self._tabs.add("Ventole")
        self._tabs.add("Temperature")

        self._fan_scroll = ctk.CTkScrollableFrame(self._tabs.tab("Ventole"), label_text="")
        self._fan_scroll.pack(fill="both", expand=True)

        self._temp_frame = ctk.CTkScrollableFrame(self._tabs.tab("Temperature"), label_text="")
        self._temp_frame.pack(fill="both", expand=True)

    def _load_snapshot(self, snap: HardwareSnapshot):
        self._load_fans(snap.fans)
        self._load_temps(snap.temps)

    def _load_fans(self, fans: list[FanSensor]):
        for w in self._fan_scroll.winfo_children():
            w.destroy()
        self._fan_rows.clear()

        if not fans:
            ctk.CTkLabel(
                self._fan_scroll,
                text="⚠ Nessuna ventola rilevata.\n\nAvvia l'app come Amministratore per leggere tutte le ventole della scheda madre.",
                font=ctk.CTkFont(size=13), text_color="#888"
            ).pack(pady=40)
            return

        hdr = ctk.CTkFrame(self._fan_scroll, fg_color="#2a2a3a")
        hdr.pack(fill="x", pady=(0, 4))
        for text, w in [
            ("Ventola / Riferimento", 280),
            ("RPM", 85),
            ("Velocità %", 220),
            ("Modalità", 105),
            ("Applica", 60),
        ]:
            ctk.CTkLabel(
                hdr, text=text, font=ctk.CTkFont(size=11, weight="bold"),
                text_color="#aaa", width=w, anchor="w" if "Ventola" in text else "center"
            ).pack(side="left", padx=6, pady=6)

        for fan in fans:
            row = _FanRow(self._fan_scroll, fan, self.ctrl)
            row.pack(fill="x", pady=2)
            self._fan_rows[fan.id] = row

    def _load_temps(self, temps: list[TempSensor]):
        for w in self._temp_frame.winfo_children():
            w.destroy()
        self._gauge_map.clear()

        if not temps:
            ctk.CTkLabel(
                self._temp_frame,
                text="⚠ Nessun sensore di temperatura rilevato.",
                font=ctk.CTkFont(size=13), text_color="#888"
            ).pack(pady=40)
            return

        grid = ctk.CTkFrame(self._temp_frame, fg_color="transparent")
        grid.pack(fill="both", expand=True, padx=8, pady=8)

        filtered = [t for t in temps if "Distance to TjMax" not in t.name]
        for i, temp in enumerate(filtered[:16]):
            col = i % 4
            row = i // 4
            lbl = temp.name[:22]
            g = GaugeWidget(
                grid,
                label=lbl,
                unit="°C",
                min_val=0, max_val=110,
                warn_pct=0.65, crit_pct=0.85,
                size=140,
                fg_color="#23233a",
            )
            g.set_value(temp.celsius)
            g.grid(row=row, column=col, padx=8, pady=8)
            self._gauge_map[temp.id] = g

    def update_snapshot(self, snap: HardwareSnapshot):
        if len(snap.fans) != len(self._fan_rows):
            self._load_fans(snap.fans)
        else:
            fan_map = {f.id: f for f in snap.fans}
            for fid, row in self._fan_rows.items():
                if fid in fan_map:
                    row.update_rpm(fan_map[fid].rpm)

        if not self._gauge_map and snap.temps:
            self._load_temps(snap.temps)
        else:
            temp_map = {t.id: t for t in snap.temps}
            for tid, gauge in self._gauge_map.items():
                if tid in temp_map:
                    gauge.set_value(temp_map[tid].celsius)

    def _rescan(self):
        snap = self.ctrl.detector.detect_all()
        self._load_snapshot(snap)


class _FanRow(ctk.CTkFrame):
    def __init__(self, master, fan: FanSensor, ctrl: AppController, **kw):
        super().__init__(master, fg_color="#1e1e2e", **kw)
        self.fan = fan
        self.ctrl = ctrl
        self._build()

    def _display_title(self) -> str:
        custom = self.ctrl.settings.get_fan_label(self.fan.id)
        main_name = custom or self.fan.name
        ref = self.fan.reference or self.fan.hardware or self.fan.id
        return f"{main_name}\nRif: {ref[:38]}"

    def _build(self):
        name_box = ctk.CTkFrame(self, fg_color="transparent", width=280)
        name_box.pack(side="left", padx=6, pady=6)
        name_box.pack_propagate(False)

        self._name_lbl = ctk.CTkLabel(
            name_box, text=self._display_title(), anchor="w", justify="left",
            font=ctk.CTkFont(size=12)
        )
        self._name_lbl.pack(side="left", fill="x", expand=True)

        ctk.CTkButton(
            name_box, text="✏", width=28, height=28,
            fg_color="#2a2a3f", hover_color="#3a3a5f",
            command=self._rename
        ).pack(side="right", padx=(4, 0))

        self._rpm_label = ctk.CTkLabel(
            self, text=f"{self.fan.rpm} RPM", width=85, anchor="center",
            font=ctk.CTkFont(size=12, weight="bold"), text_color="#38bdf8"
        )
        self._rpm_label.pack(side="left", padx=6)

        cfg = self.ctrl.settings.get_fan_settings(self.fan.id)
        initial_duty = cfg["duty_percent"] if cfg["mode"] == "manual" else self.fan.duty_percent

        self._duty_var = ctk.IntVar(value=initial_duty)
        duty_row = ctk.CTkFrame(self, fg_color="transparent", width=220)
        duty_row.pack(side="left", padx=6)
        self._duty_slider = ctk.CTkSlider(
            duty_row, from_=0, to=100, width=160,
            variable=self._duty_var, command=self._on_duty_change
        )
        self._duty_slider.pack(side="left")
        self._duty_lbl = ctk.CTkLabel(duty_row, text=f"{initial_duty}%", width=42)
        self._duty_lbl.pack(side="left", padx=4)

        self._mode_var = ctk.StringVar(value=cfg["mode"])
        if self.fan.controllable:
            ctk.CTkOptionMenu(
                self, variable=self._mode_var,
                values=["auto", "manual"], command=self._on_mode_change, width=105
            ).pack(side="left", padx=6)
            self._apply_btn = ctk.CTkButton(
                self, text="✅", width=50,
                fg_color="#16a34a", hover_color="#15803d",
                command=self._apply
            )
            self._apply_btn.pack(side="left", padx=6)
        else:
            self._duty_slider.configure(state="disabled")
            ctk.CTkLabel(
                self, text="Solo lettura (BIOS)", width=165, text_color="#666",
                font=ctk.CTkFont(size=11)
            ).pack(side="left", padx=6)

    def _rename(self):
        dlg = ctk.CTkInputDialog(
            text=f"Nome personalizzato per '{self.fan.name}'\n(lascia vuoto per ripristinare):",
            title="Rinomina Ventola"
        )
        new_name = dlg.get_input()
        if new_name is not None:
            self.ctrl.set_fan_label(self.fan.id, new_name)
            self._name_lbl.configure(text=self._display_title())

    def _on_duty_change(self, val):
        self._duty_lbl.configure(text=f"{int(float(val))}%")
        if self.fan.controllable and self._mode_var.get() != "manual":
            self._mode_var.set("manual")

    def _on_mode_change(self, mode: str):
        if mode == "auto":
            self.ctrl.set_fan_duty(self.fan.id, self.fan.control_id, int(self._duty_var.get()), "auto")

    def _apply(self):
        duty = int(self._duty_var.get())
        mode = self._mode_var.get()
        ok = self.ctrl.set_fan_duty(self.fan.id, self.fan.control_id, duty, mode)
        self._apply_btn.configure(fg_color="#16a34a" if ok else "#dc2626")

    def update_rpm(self, rpm: int):
        self._rpm_label.configure(text=f"{rpm} RPM")
