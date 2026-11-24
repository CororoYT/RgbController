"""
pages/settings_page.py – Impostazioni app.
"""
from __future__ import annotations
import customtkinter as ctk
from core.app_controller import AppController


class SettingsPage(ctk.CTkFrame):
    def __init__(self, master, controller: AppController, **kwargs):
        super().__init__(master, **kwargs)
        self.ctrl = controller
        self._build()

    def _build(self):
        self.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self, text="⚙ Impostazioni",
                     font=ctk.CTkFont(size=18, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 4)
        )

        content = ctk.CTkScrollableFrame(self)
        content.grid(row=1, column=0, sticky="nsew", padx=16, pady=(0,12))
        self.grid_rowconfigure(1, weight=1)

        # ---- OpenRGB ----
        self._section(content, "🔌 OpenRGB Server")

        row = ctk.CTkFrame(content, fg_color="transparent")
        row.pack(fill="x", pady=4)
        ctk.CTkLabel(row, text="Host:", width=80).pack(side="left")
        self._host_var = ctk.StringVar(value=self.ctrl.settings.get("openrgb_host", "127.0.0.1"))
        ctk.CTkEntry(row, textvariable=self._host_var, width=200).pack(side="left", padx=8)

        row2 = ctk.CTkFrame(content, fg_color="transparent")
        row2.pack(fill="x", pady=4)
        ctk.CTkLabel(row2, text="Porta:", width=80).pack(side="left")
        self._port_var = ctk.StringVar(value=str(self.ctrl.settings.get("openrgb_port", 6742)))
        ctk.CTkEntry(row2, textvariable=self._port_var, width=100).pack(side="left", padx=8)

        ctk.CTkButton(content, text="🔄 Riconnetti OpenRGB",
                      command=self._reconnect_openrgb, width=200).pack(anchor="w", pady=6)

        self._section(content, "🕒 Aggiornamento Sensori")
        row3 = ctk.CTkFrame(content, fg_color="transparent")
        row3.pack(fill="x", pady=4)
        ctk.CTkLabel(row3, text="Intervallo (ms):", width=140).pack(side="left")
        self._interval_var = ctk.StringVar(
            value=str(self.ctrl.settings.get("refresh_interval_ms", 2000))
        )
        ctk.CTkEntry(row3, textvariable=self._interval_var, width=100).pack(side="left", padx=8)

        self._section(content, "🎨 Tema")
        self._theme_var = ctk.StringVar(value=self.ctrl.settings.get("theme", "dark"))
        ctk.CTkSegmentedButton(
            content,
            values=["dark", "light", "system"],
            variable=self._theme_var,
            command=self._change_theme,
        ).pack(anchor="w", pady=4)

        self._section(content, "🖼 Aspetto")
        self._accent_var = ctk.StringVar(
            value=self.ctrl.settings.get("accent_color", "#1E90FF")
        )
        row4 = ctk.CTkFrame(content, fg_color="transparent")
        row4.pack(fill="x", pady=4)
        ctk.CTkLabel(row4, text="Colore accento:", width=140).pack(side="left")
        ctk.CTkEntry(row4, textvariable=self._accent_var, width=120).pack(side="left", padx=8)

        ctk.CTkFrame(content, height=1, fg_color="#333").pack(fill="x", pady=12)
        ctk.CTkButton(content, text="💾 Salva impostazioni",
                      command=self._save, fg_color="#16a34a",
                      hover_color="#15803d", height=38).pack(anchor="w")

    def _section(self, parent, title: str):
        ctk.CTkLabel(parent, text=title,
                     font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#c084fc").pack(anchor="w", pady=(14, 2))
        ctk.CTkFrame(parent, height=1, fg_color="#444").pack(fill="x", pady=(0, 6))

    def _reconnect_openrgb(self):
        self.ctrl.settings.set("openrgb_host", self._host_var.get())
        try:
            port = int(self._port_var.get())
            self.ctrl.settings.set("openrgb_port", port)
        except ValueError:
            pass
        ok = self.ctrl.rgb_ctrl.connect(
            self._host_var.get(),
            int(self._port_var.get()),
        )
        status = "✅ Connesso!" if ok else "❌ Connessione fallita"
        ctk.CTkLabel(self, text=status, text_color="#0f0" if ok else "#f00").grid(
            row=2, column=0, pady=4
        )

    def _change_theme(self, theme: str):
        ctk.set_appearance_mode(theme)
        self.ctrl.settings.set("theme", theme)

    def _save(self):
        self.ctrl.settings.set("openrgb_host", self._host_var.get())
        try:
            self.ctrl.settings.set("openrgb_port", int(self._port_var.get()))
            self.ctrl.settings.set("refresh_interval_ms", int(self._interval_var.get()))
        except ValueError:
            pass
        self.ctrl.settings.set("accent_color", self._accent_var.get())
