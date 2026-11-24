"""
main_window.py – Finestra principale dell'applicazione.
Navigazione laterale + contenuto pagine.
"""
from __future__ import annotations
import customtkinter as ctk
from core.app_controller import AppController
from core.hardware_detector import HardwareSnapshot
from gui.pages.rgb_page       import RGBPage
from gui.pages.cooler_page    import CoolerPage
from gui.pages.fan_page       import FanPage
from gui.pages.profiles_page  import ProfilesPage
from gui.pages.settings_page  import SettingsPage


NAV_ITEMS = [
    ("🌈 RGB LED",             "rgb"),
    ("💧 Raffreddamento & LCD", "cooler"),
    ("💨 Ventole & Temp",      "fans"),
    ("📁 Profili",             "profiles"),
    ("⚙ Impostazioni",        "settings"),
]


class MainWindow(ctk.CTk):
    def __init__(self, controller: AppController):
        super().__init__()
        self.ctrl = controller
        self.title("RGBController – PC Lighting & Cooling")
        self.geometry("1180x740")
        self.minsize(920, 620)
        ctk.set_appearance_mode(controller.settings.get("theme", "dark"))
        ctk.set_default_color_theme("blue")

        self._pages: dict[str, ctk.CTkFrame] = {}
        self._current_page: str = ""
        self._nav_btns: dict[str, ctk.CTkButton] = {}

        self._build_layout()
        self._navigate("rgb")

        self.ctrl.register_snapshot_callback(self._on_snapshot)
        self._update_status_bar()

    def _build_layout(self):
        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=1)

        self._sidebar = ctk.CTkFrame(self, width=215, corner_radius=0, fg_color="#12121c")
        self._sidebar.grid(row=0, column=0, sticky="nsew")
        self._sidebar.grid_propagate(False)

        logo_frame = ctk.CTkFrame(self._sidebar, fg_color="transparent")
        logo_frame.pack(pady=(20, 8), padx=12, fill="x")
        ctk.CTkLabel(logo_frame, text="🖥 RGB", font=ctk.CTkFont(size=22, weight="bold"),
                     text_color="#c084fc").pack(side="left")
        ctk.CTkLabel(logo_frame, text="Controller", font=ctk.CTkFont(size=15),
                     text_color="#a0a0c0").pack(side="left", padx=(4, 0))

        ctk.CTkFrame(self._sidebar, height=1, fg_color="#333").pack(fill="x", padx=12, pady=(0, 10))

        for label, page_id in NAV_ITEMS:
            btn = ctk.CTkButton(
                self._sidebar,
                text=label,
                anchor="w",
                height=42,
                corner_radius=8,
                fg_color="transparent",
                hover_color="#2a2a3f",
                text_color="#c0c0e0",
                font=ctk.CTkFont(size=13),
                command=lambda pid=page_id: self._navigate(pid),
            )
            btn.pack(fill="x", padx=8, pady=2)
            self._nav_btns[page_id] = btn

        ctk.CTkFrame(self._sidebar, fg_color="transparent").pack(fill="both", expand=True)

        self._openrgb_label = ctk.CTkLabel(
            self._sidebar,
            text="⬤ Motore RGB: —",
            font=ctk.CTkFont(size=11),
            text_color="#888",
        )
        self._openrgb_label.pack(pady=(0, 4), padx=12, anchor="w")

        self._status_bar = ctk.CTkLabel(
            self._sidebar,
            text="Pronto",
            font=ctk.CTkFont(size=10),
            text_color="#555",
        )
        self._status_bar.pack(pady=(0, 12), padx=12, anchor="w")

        self._content = ctk.CTkFrame(self, corner_radius=0, fg_color="#16161f")
        self._content.grid(row=0, column=1, sticky="nsew")
        self._content.grid_rowconfigure(0, weight=1)
        self._content.grid_columnconfigure(0, weight=1)

        self.ctrl.on_openrgb_status = self._on_openrgb_status

    def _navigate(self, page_id: str):
        if page_id == self._current_page:
            return

        if self._current_page and self._current_page in self._pages:
            self._pages[self._current_page].grid_remove()

        if page_id not in self._pages:
            self._pages[page_id] = self._make_page(page_id)

        self._pages[page_id].grid(row=0, column=0, sticky="nsew")
        self._current_page = page_id

        for pid, btn in self._nav_btns.items():
            if pid == page_id:
                btn.configure(fg_color="#2d2d5f", text_color="white")
            else:
                btn.configure(fg_color="transparent", text_color="#c0c0e0")

    def _make_page(self, page_id: str) -> ctk.CTkFrame:
        parent = self._content
        if page_id == "rgb":
            return RGBPage(parent, self.ctrl)
        elif page_id == "cooler":
            return CoolerPage(parent, self.ctrl)
        elif page_id == "fans":
            return FanPage(parent, self.ctrl)
        elif page_id == "profiles":
            return ProfilesPage(parent, self.ctrl)
        elif page_id == "settings":
            return SettingsPage(parent, self.ctrl)
        return ctk.CTkFrame(parent)

    def _on_snapshot(self, snap: HardwareSnapshot):
        self.after(0, lambda: self._update_ui_from_snapshot(snap))

    def _update_ui_from_snapshot(self, snap: HardwareSnapshot):
        if "fans" in self._pages:
            self._pages["fans"].update_snapshot(snap)  # type: ignore[attr-defined]
        if "cooler" in self._pages:
            self._pages["cooler"].update_snapshot(snap)  # type: ignore[attr-defined]
        self._update_status_bar()

    def _on_openrgb_status(self, connected: bool):
        self.after(0, lambda c=connected: self._openrgb_label.configure(
            text=f"⬤ Motore RGB: {'OK' if c else 'Offline'}",
            text_color="#00cc66" if c else "#ff5555",
        ))

    def _update_status_bar(self):
        snap = self.ctrl.detector.get_snapshot()
        n_rgb = len(snap.rgb_devices)
        n_fans = len(snap.fans)
        n_temp = len(snap.temps)
        self._status_bar.configure(
            text=f"{n_rgb} RGB • {n_fans} ventole • {n_temp} sensori"
        )

    def on_close(self):
        self.ctrl.stop()
        self.destroy()
