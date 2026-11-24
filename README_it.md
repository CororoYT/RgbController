<div align="center">

[English](README.md) · **Italiano**

# RGBController 🌈

Applicazione desktop Windows leggera e autonoma per il controllo di **LED RGB di Scheda Madre e header ARGB**, **RAM DDR4 e DDR5**, **periferiche**, **ventole e temperature** e **monitoraggio raffreddamento a liquido AIO** (alternativa user-friendly a TRCC), con driver nativi e salvataggio automatico delle impostazioni.

</div>

---

## Funzionalità principali

| Funzione | Descrizione |
|---|---|
| **Schede Madri Universali** | Driver nativi Windows HID per **Gigabyte** (`IT5702` / `IT8297`, inclusi header ARGB 5V `D_LED1` / `D_LED2`), **ASUS Aura Sync**, **MSI Mystic Light** e **ASRock Polychrome** |
| **RAM DDR4 & DDR5 RGB** | Supporto nativo SMBus tramite `SensorBridge` (`SMBusI801` Intel e `SMBusPiix4` AMD) per **Kingston HyperX Fury/Predator DDR4**, **ENE / Aura DRAM DDR4 & DDR5** (G.Skill Trident Z/Z5, ASUS, Kingston Fury DDR5, TeamGroup, ADATA) e **Corsair Vengeance / Dominator** |
| **Periferiche** | Supporto nativo Logitech HID++ 2.0 (`G213`, `G513`, `G610`, `G413`, `G815`) |
| **Monitor Raffreddamento & LCD** | Dashboard stile TRCC user-friendly con telemetria live (temp CPU/GPU, carico, RPM) e anteprima 1:1 personalizzabile del display AIO |
| **Ventole & Temperature** | Lettura RPM live, controllo velocità Auto/Manuale, nomi ventole personalizzabili e gauge termiche animate |
| **Sync & Profili** | Sincronizzazione con un click su tutti i dispositivi, ripristino automatico all'avvio (`config/settings.json`) e profili salvabili |

---

## Installazione rapida

**Requisiti:** Windows 10/11 (64-bit) e Python 3.10+.

1. Scarica o clona questo repository:
   ```bat
   git clone https://github.com/CororoYT/RgbController.git
   cd RgbController
   ```
2. Esegui lo script di setup automatico (installa i pacchetti Python, scarica `LibreHardwareMonitorLib.dll` e `RAMSPDToolkit-NDD.dll`, compila `SensorBridge.exe` e crea il collegamento sul Desktop):
   ```bat
   python install.py
   ```
3. Avvia l'app come **Amministratore** (necessario per leggere le ventole della scheda madre e le RAM su bus SMBus):
   - Doppio click su **`Avvia.bat`** (oppure su **`RGBController (Admin).vbs`** sul Desktop).

---

## Struttura del progetto

```
RgbController/
├── Avvia.bat                  ← Launcher Windows con elevazione UAC
├── install.py                 ← Setup automatico dipendenze e SensorBridge
├── launcher.py                ← Avvio rapido dell'app
├── main.py                    ← Entry point GUI
├── requirements.txt           ← Dipendenze Python
├── core/
│   ├── app_controller.py      ← Orchestratore centrale
│   ├── hardware_detector.py   ← Rilevamento hardware e sensori
│   ├── rgb_controller.py      ← Router driver RGB multi-marca
│   ├── fan_controller.py      ← Controllo velocità ventole
│   ├── sensor_bridge.py       ← Client IPC per SensorBridge.exe
│   ├── SensorBridge.cs        ← Servizio nativo C# (ventole, temp, RAM DDR4/DDR5 SMBus)
│   ├── settings_manager.py    ← Persistenza JSON
│   └── drivers/
│       ├── hid_win.py         ← Wrapper nativo Windows SetupAPI / HID
│       ├── it5702_hid.py      ← Driver Gigabyte IT5702 / IT8297 + D_LED1/D_LED2
│       ├── asus_aura_hid.py   ← Driver ASUS Aura Sync USB
│       ├── msi_mystic_hid.py  ← Driver MSI Mystic Light USB
│       ├── asrock_hid.py      ← Driver ASRock Polychrome USB
│       └── logitech_g213_hid.py ← Driver tastiere Logitech HID++ 2.0
└── gui/
    ├── main_window.py         ← Finestra principale e navigazione
    ├── pages/
    │   ├── rgb_page.py        ← Pagina LED RGB, zone ARGB e sync
    │   ├── cooler_page.py     ← Pagina raffreddamento & display LCD
    │   ├── fan_page.py        ← Pagina ventole e gauge temperature
    │   ├── profiles_page.py   ← Gestione profili
    │   └── settings_page.py   ← Impostazioni app
    └── widgets/
        ├── color_picker.py    ← Ruota colori HSV
        └── gauge.py           ← Gauge circolare animata
```

---

## Licenza e attribuzioni

- Codice sorgente dell'app rilasciato sotto licenza **MIT** (vedi [LICENSE](LICENSE)).
- `SensorBridge.cs` utilizza **LibreHardwareMonitorLib** e **RAMSPDToolkit** (scaricati automaticamente da `install.py` dai rispettivi repository ufficiali sotto licenza **MPL 2.0**).
