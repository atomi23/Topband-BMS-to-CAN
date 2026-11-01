# Topband / Victron Gateway — Release v2.03

Author: `@atomi23`  
Tag: `v2.03`

## Highlights

### LilyGo T-CAN485 fixes
- CAN TX/RX corrected to official pins (**GPIO26 TX / GPIO27 RX**).
- `CAN_SE` (**GPIO23**) driven LOW for high-speed CAN.
- RS485 rail/enable pins clarified (`PIN_5V_EN`, `PIN_RS485_SE`, `PIN_RS485_RE`).
- UART2 for RS485, longer timeout (1500 ms), TX settle + inter-byte frame end.
- Main poll path now uses the shared `rs485ExchangeFrame()` helper.

### M5Stack Basic/Core + Module COMMU
- New board select: `BOARD_M5STACK`.
- RS485 on GPIO16/17, CAN via MCP2515 SPI (CS12 / INT15, 500 kbit/s, 8 MHz crystal).
- Local 320×240 UI: Overview / Packs / Network (Buttons A/B/C, C toggles simulation).
- Required libraries: `M5Unified`, `mcp_can`.

### Files
- `TopbandV2_03_source.ino` — canonical source
- `TopbandV2_03_profile_LilyGo_TCAN485_WLAN.ino`
- `TopbandV2_03_profile_WaveshareS3_Minimal_WLAN.ino`
- `TopbandV2_03_profile_M5Stack_COMMU_WLAN.ino`

## Notes
- No fixed WiFi credentials in the profiles (`FIXED_SSID` / `FIXED_PASS` empty).
- Hardware live-test was not available for this release; LilyGo/M5 changes follow vendor pin docs + code review.
- Issue reference: https://github.com/atomi23/Topband-BMS-to-CAN/issues/3
