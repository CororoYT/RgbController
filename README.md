<div align="center">

**English** · [Italiano](README_it.md)

# RGBController 🌈

A lightweight, standalone Windows desktop app to control **Motherboard RGB & ARGB headers**, **DDR4 & DDR5 RAM RGB**, **peripherals**, **fan speeds & temperatures**, and **AIO liquid cooler LCD monitoring** (a user-friendly alternative to TRCC) — with native drivers and automatic settings persistence.

</div>

---

## Features

| Feature | Description |
|---|---|
| **Universal Motherboards** | Native Windows HID drivers for **Gigabyte** (`IT5702` / `IT8297`, including `D_LED1` / `D_LED2` 5V ARGB headers), **ASUS Aura Sync**, **MSI Mystic Light**, and **ASRock Polychrome** |
| **DDR4 & DDR5 RAM RGB** | Native SMBus support via `SensorBridge` (`SMBusI801` Intel & `SMBusPiix4` AMD) for **Kingston HyperX Fury/Predator DDR4**, **ENE / Aura DRAM DDR4 & DDR5** (G.Skill Trident Z/Z5, ASUS, Kingston Fury DDR5, TeamGroup, ADATA), and **Corsair Vengeance / Dominator** |
| **Peripherals** | Native Logitech HID++ 2.0 support (`G213`, `G513`, `G610`, `G413`, `G815`) |
| **Liquid Cooler & LCD Monitor** | User-friendly TRCC-style dashboard with live CPU/GPU temperatures, load telemetry, fan/pump RPM, and customizable 1:1 AIO LCD preview |
| **Fans & Temperatures** | Live RPM readout, Auto/Manual fan duty control, custom fan labels, and animated temperature gauges |
| **Sync & Profiles** | One-click sync across all devices, automatic startup restoration (`config/settings.json`), and named profiles |

---

## Quick Start

**Requirements:** Windows 10/11 (64-bit) and Python 3.10+.

1. Clone this repository:
   ```bat
   git clone https://github.com/CororoYT/RgbController.git
   cd RgbController
   ```
2. Run the automated setup script (installs Python packages, downloads `LibreHardwareMonitorLib.dll` & `RAMSPDToolkit-NDD.dll`, compiles `SensorBridge.exe`, and creates a Desktop shortcut):
   ```bat
   python install.py
   ```
3. Launch the app as **Administrator** (required for motherboard fan headers and SMBus RAM access):
   - Double-click **`Avvia.bat`** (or **`RGBController (Admin).vbs`** on your Desktop).

---

## Project Structure

```
RgbController/
├── Avvia.bat                  ← Windows UAC launcher
├── install.py                 ← One-click dependency & SensorBridge setup
├── launcher.py                ← App launcher
├── main.py                    ← GUI entry point
├── requirements.txt           ← Python dependencies
├── core/
│   ├── app_controller.py      ← Central orchestrator
│   ├── hardware_detector.py   ← Hardware & sensor auto-detection
│   ├── rgb_controller.py      ← Multi-vendor RGB driver router
│   ├── fan_controller.py      ← Fan speed controller
│   ├── sensor_bridge.py       ← IPC client for SensorBridge.exe
│   ├── SensorBridge.cs        ← Native C# service (fans, temps, DDR4/DDR5 SMBus RAM)
│   ├── settings_manager.py    ← JSON persistence
│   └── drivers/
│       ├── hid_win.py         ← Native Windows SetupAPI / HID wrapper
│       ├── it5702_hid.py      ← Gigabyte IT5702 / IT8297 + D_LED1/D_LED2 driver
│       ├── asus_aura_hid.py   ← ASUS Aura Sync USB driver
│       ├── msi_mystic_hid.py  ← MSI Mystic Light USB driver
│       ├── asrock_hid.py      ← ASRock Polychrome USB driver
│       └── logitech_g213_hid.py ← Logitech HID++ 2.0 keyboard driver
└── gui/
    ├── main_window.py         ← Main window & sidebar navigation
    ├── pages/
    │   ├── rgb_page.py        ← RGB LED, ARGB zones & sync page
    │   ├── cooler_page.py     ← Liquid cooler & LCD monitor page
    │   ├── fan_page.py        ← Fans & temperature gauges page
    │   ├── profiles_page.py   ← Profiles manager
    │   └── settings_page.py   ← App settings
    └── widgets/
        ├── color_picker.py    ← HSV color wheel
        └── gauge.py           ← Animated circular gauge
```

---

## License & Attributions

- Application source code is licensed under the **MIT License** (see [LICENSE](LICENSE)).
- `SensorBridge.cs` uses **LibreHardwareMonitorLib** and **RAMSPDToolkit** (automatically downloaded by `install.py` from their official releases under **MPL 2.0**).
