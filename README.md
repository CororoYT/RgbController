<div align="center">

**English** · [Italiano](README_it.md)

# RGBController 🌈

A lightweight, ultra-fast **native C11 Windows app** (~144 KB) to control **Motherboard RGB & ARGB headers**, **DDR4 & DDR5 RAM RGB**, **peripherals**, **fan speeds & temperatures**, and **AIO liquid cooler LCD monitoring** (a user-friendly alternative to TRCC) — with automatic UAC elevation and zero Python runtime required.

</div>

---

## Features

| Feature | Description |
|---|---|
| **Native C11 + Win32 Dark UI** | Double-buffered GDI interface, pre-rendered HSV color wheel, < 5 ms startup, ~144 KB executable |
| **Universal Motherboards** | Native Windows HID drivers for **Gigabyte** (`IT5702` / `IT8297`, including `D_LED1` / `D_LED2` 5V ARGB headers), **ASUS Aura Sync**, **MSI Mystic Light**, and **ASRock Polychrome** |
| **DDR4 & DDR5 RAM RGB** | Native SMBus support via `SensorBridge` (`SMBusI801` Intel & `SMBusPiix4` AMD) for **Kingston HyperX Fury/Predator DDR4**, **ENE / Aura DRAM DDR4 & DDR5** (G.Skill Trident Z/Z5, ASUS, Kingston Fury DDR5, TeamGroup, ADATA), and **Corsair Vengeance / Dominator** |
| **Peripherals** | Native Logitech HID++ 2.0 support (`G213`, `G513`, `G610`, `G413`, `G815`) |
| **Liquid Cooler & LCD Monitor** | User-friendly TRCC-style dashboard with live CPU/GPU temperatures, load telemetry, fan/pump RPM, and customizable 1:1 AIO LCD preview |
| **Fans & Temperatures** | Live RPM readout, continuous fan duty sliders (Auto/Manual), and animated circular temperature gauges |
| **Profiles & Auto-Elevation** | Save/load named profiles, startup profile support, and automatic Administrator elevation at launch |

> **Note:** The previous Python/CustomTkinter version is preserved on branch `backup-v2.0` (tag `v2.0-backup`).

---

## Quick Start

### Option A: One-Click Installer (Recommended — No compiler needed)
1. Download **`RGBController-Portable.zip`** (or **`Install.bat`**) from the [Latest Release](https://github.com/CororoYT/RgbController/releases/latest).
2. Extract and run **`Install.bat`** (automatically installs to `C:\Program Files\RGBController` and creates Desktop & Start Menu shortcuts) — or just double-click **`RGBController.exe`** directly.

### Option B: Build from Source

**Requirements:** Windows 10/11 (64-bit), MinGW-w64 `gcc` (for `RGBController.exe`), and `.NET Framework 4.x csc.exe` + `LibreHardwareMonitorLib.dll` / `RAMSPDToolkit-NDD.dll` in `tools/LibreHardwareMonitor/` (for `SensorBridge.exe`).

1. Clone this repository:
   ```bat
   git clone https://github.com/CororoYT/RgbController.git
   cd RgbController
   ```
2. Compile `RGBController.exe`:
   ```bat
   build.bat
   ```
3. Double-click **`RGBController.exe`** (automatically requests Administrator privileges for SMBus RAM & motherboard fans).

---

## Project Structure

```
RgbController/
├── build.bat                  ← GCC -O3 build script for RGBController.exe
├── LICENSE                    ← MIT License
├── README.md                  ← Documentation (English)
├── README_it.md               ← Documentation (Italiano)
└── src/
    ├── main.c                 ← Win32 Dark-Mode 5-page GUI & UAC elevation
    ├── hid_drivers.h / .c     ← Native SetupAPI / HID multi-vendor RGB drivers
    ├── sensor_ipc.h / .c      ← Win32 pipe IPC & zero-alloc JSON parser
    ├── settings.h / .c        ← Native INI settings & profiles persistence
    └── SensorBridge.cs        ← Native C# service (fans, temps, DDR4/DDR5 SMBus RAM)
```

---

## License & Attributions

- Application source code is licensed under the **MIT License** (see [LICENSE](LICENSE)).
- `SensorBridge.cs` uses **LibreHardwareMonitorLib** and **RAMSPDToolkit** under **MPL 2.0**.
