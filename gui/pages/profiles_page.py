"""
pages/profiles_page.py – Gestione profili (salva/carica/elimina).
"""
from __future__ import annotations
import customtkinter as ctk
from core.app_controller import AppController


class ProfilesPage(ctk.CTkFrame):
    def __init__(self, master, controller: AppController, **kwargs):
        super().__init__(master, **kwargs)
        self.ctrl = controller
        self._build()
        self._refresh_list()

    def _build(self):
        self.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self, text="📁 Gestione Profili",
                     font=ctk.CTkFont(size=18, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 4)
        )
        ctk.CTkLabel(self, text="Salva l'intera configurazione (RGB + ventole) in un profilo nominato.",
                     font=ctk.CTkFont(size=12), text_color="#888").grid(
            row=1, column=0, sticky="w", padx=16, pady=(0, 10)
        )

        # Nuovo profilo
        new_frame = ctk.CTkFrame(self, fg_color="#2a2a3a")
        new_frame.grid(row=2, column=0, sticky="ew", padx=16, pady=4)
        ctk.CTkLabel(new_frame, text="Nuovo profilo:", font=ctk.CTkFont(size=12)).pack(
            side="left", padx=12, pady=10
        )
        self._new_name = ctk.CTkEntry(new_frame, placeholder_text="Nome profilo…", width=240)
        self._new_name.pack(side="left", padx=8)
        ctk.CTkButton(new_frame, text="💾 Salva corrente", command=self._save_profile,
                      fg_color="#16a34a", hover_color="#15803d").pack(side="left", padx=8)

        # Lista profili
        ctk.CTkLabel(self, text="Profili salvati",
                     font=ctk.CTkFont(size=13, weight="bold")).grid(
            row=3, column=0, sticky="w", padx=16, pady=(14, 4)
        )
        self._list_frame = ctk.CTkScrollableFrame(self, label_text="")
        self._list_frame.grid(row=4, column=0, sticky="nsew", padx=16, pady=(0, 12))
        self.grid_rowconfigure(4, weight=1)

        # Profilo di avvio
        boot_frame = ctk.CTkFrame(self, fg_color="#2a2a3a")
        boot_frame.grid(row=5, column=0, sticky="ew", padx=16, pady=(0, 12))
        ctk.CTkLabel(boot_frame, text="Profilo di avvio:", font=ctk.CTkFont(size=12)).pack(
            side="left", padx=12, pady=10
        )
        self._startup_var = ctk.StringVar(value=self.ctrl.settings.get("startup_profile") or "Nessuno")
        profiles = ["Nessuno"] + self.ctrl.list_profiles()
        self._startup_menu = ctk.CTkOptionMenu(
            boot_frame, variable=self._startup_var,
            values=profiles, command=self._set_startup_profile, width=200
        )
        self._startup_menu.pack(side="left", padx=8)

    def _save_profile(self):
        name = self._new_name.get().strip()
        if not name:
            return
        self.ctrl.save_profile(name)
        self._new_name.delete(0, "end")
        self._refresh_list()

    def _refresh_list(self):
        for w in self._list_frame.winfo_children():
            w.destroy()

        profiles = self.ctrl.list_profiles()
        self._startup_menu.configure(values=["Nessuno"] + profiles)

        if not profiles:
            ctk.CTkLabel(self._list_frame, text="Nessun profilo salvato.",
                         text_color="#666").pack(pady=20)
            return

        for name in profiles:
            row = ctk.CTkFrame(self._list_frame, fg_color="#1e1e2e")
            row.pack(fill="x", pady=2)
            ctk.CTkLabel(row, text=f"📄 {name}", anchor="w", width=250).pack(
                side="left", padx=12, pady=8
            )
            ctk.CTkButton(row, text="▶ Carica", width=90,
                          command=lambda n=name: self._load_profile(n),
                          fg_color="#1d4ed8", hover_color="#1e40af").pack(side="left", padx=4)
            ctk.CTkButton(row, text="🗑 Elimina", width=90,
                          command=lambda n=name: self._delete_profile(n),
                          fg_color="#991b1b", hover_color="#7f1d1d").pack(side="left", padx=4)

    def _load_profile(self, name: str):
        self.ctrl.load_profile(name)

    def _delete_profile(self, name: str):
        self.ctrl.delete_profile(name)
        self._refresh_list()

    def _set_startup_profile(self, name: str):
        val = None if name == "Nessuno" else name
        self.ctrl.settings.set("startup_profile", val)
