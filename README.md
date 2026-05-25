# BK3266 Controller
Bluetooth Sender für "jede" Audioquelle, sucht und findet Bluetooth Empfänger (Kopfhörer/Headsets/Lautsprecher usw.) und lässt den Benutzer wählen, womit die Verbindung hergestellt wird. 
Ausserdem kann man auch ein EQ Preset aus dem Bluetooth Transmitter auswählen, um den Klang anzupassen.

Bluetooth Audio Transmitter Controller für Heltec WiFi Kit 32 (V1) + BK3266 / F-6988 V3.1.

## Hardware

| Bauteil | Verbindung |
|---|---|
| OLED SDA | GPIO4 (intern) |
| OLED SCL | GPIO15 (intern) |
| OLED RST | GPIO16 (intern) |
| BK3266 RX | GPIO17 (ESP TX) |
| BK3266 TX | GPIO18 (ESP RX) |
| Taster UP | GPIO25 → GND |
| Taster DOWN | GPIO26 → GND |
| Taster ENTER | GPIO27 → GND |

BK3266 UART: 9600 Baud, 8N1, 3.3V direkt (kein Level Shifter).

## Bedienung

| Taste | Aktion |
|---|---|
| UP | Cursor hoch / Wert erhöhen |
| DOWN | Cursor runter / Wert verringern |
| ENTER kurz | Bestätigen / Auswählen |
| ENTER lang | Zurück / Abbrechen / Speichern |

## Menüstruktur

```
IDLE
└── ENTER ──> HAUPTMENÜ
              ├── Pairing       (BT+PR)
              ├── Verbinden     (BT+AC, letztes Gerät)
              ├── Trennen       (BT+DC)
              ├── Lautstärke    (0–16)
              ├── EQ            (9 Presets)
              └── Einstellungen
                  ├── Auto-Reconnect  EIN/AUS
                  ├── Beep            EIN/AUS
                  └── BT-Name         (Zeicheneditor)
```

Shortcut: ENTER lang im IDLE-Screen startet direkt Pairing.

## Bootlogo

1. `logo_converter.html` im Browser öffnen
2. Bild laden (128×64, S/W) oder direkt im Pixel-Editor zeichnen
3. → "Generieren" → "logo.h speichern"
4. `include/logo.h` ersetzen
5. In `logo.h` die Zeile `#define LOGO_BMP_DEFINED` einkommentieren

Ohne Logo zeigt der Splash-Screen Text-Fallback.

## Build

```bash
pio run                  # kompilieren
pio run --target upload  # flashen
pio device monitor       # Serial Monitor
```
