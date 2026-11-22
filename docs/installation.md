# Installation und Flashen

**Deutsch** · [English](installation.en.md)

[← Dokumentationsübersicht](../README.md)

Auf dieser Seite: [Downloads](#downloads) · [Code konfigurieren](#code-konfigurieren) · [Arduino-IDE-Einstellungen](#arduino-ide-einstellungen) · [Erster Start](#erster-start) · [Neu in V2.03](#neu-in-v203)

## Downloads

V2.03 ist universell — entweder fertiges **Profil** flashen/kompilieren oder im Source oben Board + Modus wählen.

Aktuelles Release: **[v2.03](https://github.com/atomi23/Topband-BMS-to-CAN/releases/tag/v2.03)**

| Datei | Zweck |
|---|---|
| `TopbandV2_03_source.ino` | Maßgeblicher Quellcode (Board oben wählen) |
| `TopbandV2_03_profile_LilyGo_TCAN485_WLAN.ino` | LilyGo fertig vorkonfiguriert |
| `TopbandV2_03_profile_WaveshareS3_Minimal_WLAN.ino` | Waveshare S3 fertig vorkonfiguriert |
| `TopbandV2_03_profile_M5Stack_COMMU_WLAN.ino` | M5Stack + COMMU (Libs: M5Unified, mcp_can) |
| `TopbandV2_03_LilyGo_TCAN485_WLAN_merged.bin` | Web-Flasher (Offset `0x0`) |
| `TopbandV2_03_M5Stack_COMMU_WLAN_merged.bin` | Web-Flasher (Offset `0x0`) |

Flashen z.B. über [web.esphome.io](https://web.esphome.io/) oder [Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) — **merged.bin** wählen.

## Code konfigurieren

Öffne `TopbandV2_03_source.ino` (oder ein `TopbandV2_03_profile_*.ino`) in der Arduino IDE. Ganz oben den Konfigurations-Bereich setzen:

```cpp
// 1. HARDWARE (Wähle GENAU EINS)
#define BOARD_LILYGO        // T-CAN485
// #define BOARD_WAVESHARE  // Waveshare S3 (Mini & Power)
// #define BOARD_M5STACK    // M5Stack Basic/Core + Module COMMU

// 2. MODUS (Wähle GENAU EINS)
#define MODE_SMART_WIFI     // Standard (Webinterface & HA)
// #define MODE_SIMPLE_CABLE // Stealth (kein WLAN)
```

Fertige Profile (Board schon gesetzt):
- `TopbandV2_03_profile_LilyGo_TCAN485_WLAN.ino`
- `TopbandV2_03_profile_WaveshareS3_Minimal_WLAN.ino`
- `TopbandV2_03_profile_M5Stack_COMMU_WLAN.ino` (Libs: **M5Unified**, **mcp_can**)

Ein Code für alle Boards. Hardware und Modus oben im Sketch wählen:
- **Hardware:** `LilyGo T-CAN485`, `Waveshare ESP32-S3` (Mini/Power), `M5Stack + COMMU`.
- **Modus:**
  - `SMART_WIFI`: Volles Programm mit Webinterface, MQTT, Home Assistant & Logging.
  - `SIMPLE_CABLE`: "Stealth"-Modus ohne WLAN. Startzeit < 1 Sekunde, Plug & Play Übersetzung.

Die unterstützten Boards und Bezugsquellen stehen unter [Hardware und Verkabelung](hardware.md#bezugsquellen).

## Arduino-IDE-Einstellungen

Wähle unter Werkzeuge -> Board die passenden Settings:

| Einstellung      | LilyGo T-CAN485       | Waveshare S3 (Mini & Power) | M5Stack Basic + COMMU |
|---|---|---|---|
| Board            | ESP32 Dev Module      | ESP32S3 Dev Module          | M5Stack-Core-ESP32 / M5Stack Classic |
| USB CDC On Boot  | - (egal)              | Enable (Wichtig!)           | - |
| Flash Mode       | QIO                   | QIO 80MHz                   | QIO |
| Partition Scheme | Huge APP (3MB No OTA) | 8MB with Spiffs (oder 16MB) | Huge APP / Default |

- Tipp Waveshare S3: BOOT halten, kurz RESET, BOOT loslassen.
- M5 COMMU: RS485 A/B und CAN H/L am Modul; Display-UI zeigt Overview/Packs/Network.

## Erster Start

- Verbinde dich mit dem WLAN `Victron-Gateway-Setup`.
- Öffne `http://192.168.4.1` im Browser.
- Konfiguriere dein Haus-WLAN.
- Stelle unter **Einstellungen & Tools → Allgemein → BMS Anzahl** die Anzahl der angeschlossenen BMS ein. Im aktuellen `main` sind **1 bis 16** zulässig; beim Laden und Speichern begrenzt die Firmware den Wert auf diesen Bereich.
- Wichtig: Gehe danach in **Einstellungen & Tools → Profi Modus** und prüfe die Zellanzahl (15S oder 16S)!

Weiter: [Webinterface und Einstellungen](webinterface.md) oder [MQTT und Home Assistant](mqtt.md).

## Neu in V2.03

- **LilyGo T-CAN485 Fix:** CAN TX/RX an offizielle Pins (GPIO26/27), `CAN_SE` GPIO23, robusteres RS485-Polling (Timeout/Frame-Ende, UART2).
- **M5Stack Basic/Core + COMMU:** RS485 + MCP2515-CAN, lokale UI auf dem 320×240 Display (Buttons A/B/C).
- Source: `TopbandV2_03_source.ino` bzw. fertige Profile `TopbandV2_03_profile_*`.

[Release Notes (Englisch)](../RELEASE_NOTES_V2_03.md)
