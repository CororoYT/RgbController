<div align="center">

[English](README.md) · **Italiano**

# RGBController 🌈

Applicazione Windows **nativa in C11** leggera e velocissima (~144 KB) per il controllo di **LED RGB di Scheda Madre e header ARGB**, **RAM DDR4 e DDR5**, **periferiche**, **ventole e temperature** e **monitoraggio raffreddamento a liquido AIO** (alternativa user-friendly a TRCC) — con richiesta automatica dei permessi Amministratore e senza dipendere da Python.

</div>

---

## Funzionalità principali

| Funzione | Descrizione |
|---|---|
| **C11 Nativo + UI Dark Win32** | Interfaccia GDI double-buffered senza sfarfallio, ruota HSV pre-renderizzata, avvio istantaneo (~144 KB) |
| **Schede Madri Universali** | Driver nativi Windows HID per **Gigabyte** (`IT5702` / `IT8297`, inclusi header ARGB 5V `D_LED1` / `D_LED2`), **ASUS Aura Sync**, **MSI Mystic Light** e **ASRock Polychrome** |
| **RAM DDR4 & DDR5 RGB** | Supporto nativo SMBus tramite `SensorBridge` (`SMBusI801` Intel e `SMBusPiix4` AMD) per **Kingston HyperX Fury/Predator DDR4**, **ENE / Aura DRAM DDR4 & DDR5** (G.Skill Trident Z/Z5, ASUS, Kingston Fury DDR5, TeamGroup, ADATA) e **Corsair Vengeance / Dominator** |
| **Periferiche** | Supporto nativo Logitech HID++ 2.0 (`G213`, `G513`, `G610`, `G413`, `G815`) |
| **Monitor Raffreddamento & LCD** | Dashboard stile TRCC user-friendly con telemetria live (temp CPU/GPU, carico, RPM) e anteprima 1:1 personalizzabile del display AIO |
| **Ventole & Temperature** | Lettura RPM live, slider continuo velocità (Auto/Manuale) e gauge circolari animate per tutte le temperature |
| **Profili & Elevazione UAC** | Salvataggio/caricamento profili, profilo di avvio automatico e richiesta UAC automatica all'apertura dell'exe |

> **Nota:** La precedente versione in Python/CustomTkinter è conservata nel branch `backup-v2.0` (tag `v2.0-backup`).

---

## Installazione e Avvio rapido

### Opzione A: Installer Automatico (Consigliato — Nessun compilatore richiesto)
1. Scarica **`RGBController-Portable.zip`** (oppure **`Install.bat`**) dalla [Latest Release](https://github.com/CororoYT/RgbController/releases/latest).
2. Estrai e avvia **`Install.bat`** (installa in `C:\Program Files\RGBController` e crea i collegamenti su Desktop e Menu Start) — oppure fai doppio click diretto su **`RGBController.exe`**.

### Opzione B: Compilazione dai Sorgenti

**Requisiti:** Windows 10/11 (64-bit), MinGW-w64 `gcc` (per `RGBController.exe`), e `.NET Framework 4.x csc.exe` + `LibreHardwareMonitorLib.dll` / `RAMSPDToolkit-NDD.dll` in `tools/LibreHardwareMonitor/` (per `SensorBridge.exe`).

1. Clona il repository:
   ```bat
   git clone https://github.com/CororoYT/RgbController.git
   cd RgbController
   ```
2. Compila `RGBController.exe`:
   ```bat
   build.bat
   ```
3. Fai doppio click su **`RGBController.exe`** (richiede automaticamente i permessi Amministratore per RAM SMBus e ventole della scheda madre).

---

## Struttura del progetto

```
RgbController/
├── build.bat                  ← Script di compilazione GCC -O3
├── LICENSE                    ← Licenza MIT
├── README.md                  ← Documentazione (English)
├── README_it.md               ← Documentazione (Italiano)
└── src/
    ├── main.c                 ← GUI Win32 Dark-Mode (5 schede) + elevazione UAC
    ├── hid_drivers.h / .c     ← Driver nativi SetupAPI / HID multi-marca
    ├── sensor_ipc.h / .c      ← Pipe IPC Win32 & parser JSON a zero allocazioni
    ├── settings.h / .c        ← Persistenza impostazioni e profili
    └── SensorBridge.cs        ← Servizio nativo C# (ventole, temp, RAM DDR4/DDR5 SMBus)
```

---

## Licenza e attribuzioni

- Codice sorgente dell'app rilasciato sotto licenza **MIT** (vedi [LICENSE](LICENSE)).
- `SensorBridge.cs` utilizza **LibreHardwareMonitorLib** e **RAMSPDToolkit** sotto licenza **MPL 2.0**.
