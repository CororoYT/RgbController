# RGBController 🌈

App desktop Windows per il controllo unificato di **LED RGB** (scheda madre, ventole ARGB, RAM, periferiche), **ventole e temperature** e **monitoraggio raffreddamento a liquido AIO** (alternativa user-friendly a TRCC), con driver nativi e persistenza automatica delle impostazioni.

## Funzionalità principali

| Funzione | Descrizione |
|---|---|
| **Driver RGB nativi** | Controllo diretto via Windows HID per schede madri Gigabyte (`IT5702` / `IT8297`, inclusi header ARGB `D_LED1`/`D_LED2`) e tastiere Logitech HID++ (`G213`, ecc.) |
| **RAM RGB su SMBus** | Controllo nativo delle RAM HyperX / Kingston Fury DDR4 RGB via `SensorBridge` (`SMBusI801`) |
| **Monitor Raffreddamento & LCD** | Dashboard stile TRCC user-friendly con telemetria live (CPU/GPU temp, carico, RPM) e anteprima display AIO personalizzabile |
| **Ventole & Temperature** | Lettura RPM, regolazione velocità (Auto/Manual), nomi personalizzabili e gauge termiche animate |
| **Profili & Persistenza** | Salvataggio automatico dell'ultima configurazione in `config/settings.json` e gestione profili |

## Installazione rapida (da fare una volta)

Requisiti: **Windows 10/11 (x64)** e **Python 3.10+**.

1. Scarica o clona questo repository:
   ```bat
   git clone https://github.com/CororoYT/RgbController.git
   cd RgbController
   ```
2. Esegui lo script di setup automatico (scarica `LibreHardwareMonitorLib.dll`, installa i pacchetti Python, compila `SensorBridge.exe` e crea il collegamento sul Desktop):
   ```bat
   python install.py
   ```
3. Avvia l'app con privilegi **Amministratore** (necessari per leggere ventole della scheda madre e RAM SMBus):
   - Doppio click su **`Avvia.bat`** (oppure sul collegamento **`RGBController (Admin).vbs`** creato sul Desktop).

## Struttura del progetto

```
RgbController/
├── Avvia.bat                  ← Launcher Windows con elevazione UAC
├── install.py                 ← Setup automatico dipendenze e compilazione SensorBridge
├── launcher.py                ← Avvio rapido dell'app
├── main.py                    ← Entry point GUI
├── requirements.txt           ← Dipendenze Python
├── core/
│   ├── app_controller.py      ← Orchestratore centrale
│   ├── hardware_detector.py   ← Rilevamento hardware e sensori
│   ├── rgb_controller.py      ← Router driver RGB
│   ├── fan_controller.py      ← Controllo velocità ventole
│   ├── sensor_bridge.py       ← Client IPC per SensorBridge.exe
│   ├── SensorBridge.cs        ← Servizio nativo C# (sensori, ventole, SMBus RAM)
│   ├── settings_manager.py    ← Persistenza JSON
│   └── drivers/
│       ├── hid_win.py         ← Wrapper nativo Windows SetupAPI / HID
│       ├── it5702_hid.py      ← Driver Gigabyte IT5702 / IT8297 + D_LED1/D_LED2
│       └── logitech_g213_hid.py ← Driver tastiere Logitech HID++ 2.0
└── gui/
    ├── main_window.py         ← Finestra principale + sidebar
    ├── pages/
    │   ├── rgb_page.py        ← Controllo RGB LED, zone ARGB e sync
    │   ├── cooler_page.py     ← Monitor raffreddamento & display LCD
    │   ├── fan_page.py        ← Ventole & gauge temperature
    │   ├── profiles_page.py   ← Gestione profili
    │   └── settings_page.py   ← Impostazioni app
    └── widgets/
        ├── color_picker.py    ← Ruota colori HSV
        └── gauge.py           ← Gauge circolare animata
```

## Licenza e attribuzioni

- Codice sorgente dell'app rilasciato sotto licenza **MIT** (vedi `LICENSE`).
- Il modulo `SensorBridge.cs` si appoggia alle librerie **LibreHardwareMonitorLib** e **RAMSPDToolkit** (scaricate automaticamente da `install.py` dai rispettivi repository ufficiali sotto licenza **MPL 2.0**).
