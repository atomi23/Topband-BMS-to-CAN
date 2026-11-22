# Installation and flashing

[Deutsch](installation.md) · **English**

[← Documentation overview](../README.en.md)

On this page: [Downloads](#downloads) · [Configure the code](#configure-the-code) · [Arduino IDE settings](#arduino-ide-settings) · [First start](#first-start) · [What's new in V2.03](#whats-new-in-v203)

## Downloads

Flash or compile a ready-made profile, or select your board and mode in the source sketch.

Current release: **[v2.03](https://github.com/atomi23/Topband-BMS-to-CAN/releases/tag/v2.03)**

| File | Purpose |
|---|---|
| `TopbandV2_03_source.ino` | Canonical source (choose a board at the top) |
| `TopbandV2_03_profile_LilyGo_TCAN485_WLAN.ino` | Preconfigured LilyGo profile |
| `TopbandV2_03_profile_WaveshareS3_Minimal_WLAN.ino` | Preconfigured Waveshare S3 profile |
| `TopbandV2_03_profile_M5Stack_COMMU_WLAN.ino` | M5Stack + COMMU profile (libraries: M5Unified, mcp_can) |
| `TopbandV2_03_LilyGo_TCAN485_WLAN_merged.bin` | Web flasher (offset `0x0`) |
| `TopbandV2_03_M5Stack_COMMU_WLAN_merged.bin` | Web flasher (offset `0x0`) |

Use [web.esphome.io](https://web.esphome.io/) or [Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) and select the **merged.bin** file from the release.

## Configure the code

Open `TopbandV2_03_source.ino` (or a ready `TopbandV2_03_profile_*.ino`) in Arduino IDE. At the top, select exactly one board and mode:

```cpp
// 1. HARDWARE (Select ONE)
#define BOARD_LILYGO        // <-- For T-CAN485
// #define BOARD_WAVESHARE  // <-- For Waveshare S3 (Mini & Power)
// #define BOARD_M5STACK    // <-- M5Stack Basic/Core + COMMU

// 2. MODE (Select ONE)
#define MODE_SMART_WIFI     // <-- Standard (WiFi/Web)
// #define MODE_SIMPLE_CABLE // <-- Stealth (No WiFi)
```

One firmware for all hardware, configurable via `#define`:
- LilyGo T-CAN485 (Classic)
- Waveshare ESP32-S3 (Mini & Power/Pro)
- M5Stack Basic/Core + COMMU (RS485 + MCP2515 CAN)

Modes:
- `SMART_WIFI`: Full features including WebUI, MQTT, HA & Logging.
- `SIMPLE_CABLE`: "Stealth" mode without WiFi. Boot time < 1 sec, plug & play translation.

See [Hardware and wiring](hardware.en.md#where-to-buy) for buying links.

## Arduino IDE settings

| Setting          | LilyGo T-CAN485       | Waveshare S3 (Mini & Power) | M5Stack Basic + COMMU |
|---|---|---|---|
| Board            | ESP32 Dev Module      | ESP32S3 Dev Module          | M5Stack-Core-ESP32 |
| USB CDC On Boot  | -                     | Enable (Crucial!)           | - |
| Flash Mode       | QIO                   | QIO 80MHz                   | QIO |
| Partition Scheme | Huge APP (3MB No OTA) | 8MB with Spiffs (or 16MB)   | Huge APP / Default |

- Tip for Waveshare S3: If upload fails, hold BOOT, press RESET, then release BOOT.
- M5 COMMU needs **M5Unified** and **mcp_can**. The module exposes RS485 A/B and CAN H/L; the display shows Overview, Packs and Network.

## First start

- Connect to WiFi hotspot `Victron-Gateway-Setup`.
- Open `http://192.168.4.1`.
- Configure your home WiFi.
- Set the number of connected BMS units under **Settings & Tools → General → BMS Count**. Current `main` accepts **1 to 16**; the firmware clamps the value to this range when loading and saving it.
- Important: Go to **Settings & Tools → Expert Mode** and verify Cell Count (15S/16S)!

Next: [Web interface and settings](webinterface.en.md) or [MQTT and Home Assistant](mqtt.en.md).

## What's new in V2.03

- **LilyGo T-CAN485 fix:** CAN TX/RX on the official pins (GPIO26/27), `CAN_SE` on GPIO23, and more robust RS485 polling (UART2, timeout and end-of-frame detection).
- **M5Stack Basic/Core + COMMU:** RS485 and MCP2515 CAN, plus a local UI on the 320×240 display (buttons A/B/C).
- The changes are in `TopbandV2_03_source.ino` and the ready-made `TopbandV2_03_profile_*` sketches.

[Release notes](../RELEASE_NOTES_V2_03.md)
