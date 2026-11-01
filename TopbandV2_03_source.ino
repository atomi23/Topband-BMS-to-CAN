/* TOPBAND / VICTRON GATEWAY V2.03 - LilyGo Fix + M5Stack COMMU
   - Fix: MQTT "Full Data" liefert BMS-Detaildaten inkl. Zellspannungen
   - Fix: Robuste Grad-Anzeige im Webinterface (&deg; statt Encoding-Probleme)
   - Fix: Klarere Labels fuer Einstellfelder im Webinterface
   - Hinweis: Teile der Protokoll-Erweiterungen basieren auf Reverse-Engineering-Arbeit von
              https://github.com/linedot/topband-bms
*/

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

// ==========================================
// 1. HARDWARE (genau EINS aktivieren)
// ==========================================
// #define BOARD_LILYGO
#define BOARD_WAVESHARE     // <-- Default
// #define BOARD_M5STACK     // M5Stack Basic/Core + Module COMMU

// ==========================================
// 2. MODUS (WIFI fix ausgewaehlt)
// ==========================================
#define MODE_SMART_WIFI
// #define MODE_SIMPLE_CABLE
// ==========================================

const char* FIXED_SSID = ""; 
const char* FIXED_PASS = "";
const char *HOSTNAME = "Topband-Gateway"; 
const char *FW_VERSION = "2.03";

// --- POLL COMMANDS ---
const char *POLL_CMDS[16] = {
    "~21004642E00200FD36\r", "~21014642E00201FD34\r", "~21024642E00202FD32\r",
    "~21034642E00203FD30\r", "~21044642E00204FD2E\r", "~21054642E00205FD2C\r",
    "~21064642E00206FD2A\r", "~21074642E00207FD28\r", "~21084642E00208FD26\r",
    "~21094642E00209FD24\r", "~210A4642E0020AFD14\r", "~210B4642E0020BFD12\r",
    "~210C4642E0020CFD10\r", "~210D4642E0020DFD0E\r", "~210E4642E0020EFD0C\r",
    "~210F4642E0020FFD0A\r"};

#define NUM_LEDS 1
#define MAX_BMS 16      
#define RS485_BAUD 9600
#define POLL_INTERVAL 3000
#define RS485_TIMEOUT 1500
#define BUS_GUARD_TIME 300
#define UI_INTERVAL 1000
#define WDT_TIMEOUT 10 
#define HISTORY_LEN 960 
#define HISTORY_INTERVAL 180000 
#define SD_LOG_INTERVAL 60000
#define MQTT_INTERVAL 5000
#define ALARM_POLL_INTERVAL 15000
#define SYSPARAM_POLL_INTERVAL 30000
#define TB_INFO_MAX 256

#if defined(BOARD_LILYGO)
  // Official LilyGO T-CAN485 config.h pin map
  #define RS485_TX_PIN 22
  #define RS485_RX_PIN 21
  #define PIN_5V_EN 16        // ME2107 boost
  #define PIN_RS485_SE 19     // MAX13487E SHDN (HIGH=enable)
  #define PIN_RS485_RE 17     // MAX13487E /RE (HIGH=autodirection)
  #define CAN_TX_PIN 26       // fixed: was swapped in <=2.02
  #define CAN_RX_PIN 27
  #define PIN_CAN_SE 23       // SN65HVD231 RS (LOW=high-speed)
  #define LED_PIN 4
  #define HAS_SD_CARD true
  #define HAS_NEOPIXEL 1
  #define HAS_TWAI 1
  #define HAS_M5_UI 0
  #define SD_MISO 2
  #define SD_MOSI 15
  #define SD_SCLK 14
  #define SD_CS 13
#elif defined(BOARD_WAVESHARE)
  #define RS485_TX_PIN 17
  #define RS485_RX_PIN 18
  #define PIN_RS485_DIR 21
  #define CAN_TX_PIN 15
  #define CAN_RX_PIN 16
  #define LED_PIN 38
  #define HAS_SD_CARD false
  #define HAS_NEOPIXEL 1
  #define HAS_TWAI 1
  #define HAS_M5_UI 0
  #define SD_MISO -1
  #define SD_MOSI -1
  #define SD_SCLK -1
  #define SD_CS -1
#elif defined(BOARD_M5STACK)
  // M5Stack Basic/Core + Module COMMU (RS485 UART2 + MCP2515 SPI CAN)
  #define RS485_TX_PIN 16
  #define RS485_RX_PIN 17
  #define CAN_CS_PIN 12
  #define CAN_INT_PIN 15
  #define CAN_SPI_SCK 18
  #define CAN_SPI_MISO 19
  #define CAN_SPI_MOSI 23
  #define LED_PIN -1
  #define HAS_SD_CARD false
  #define HAS_NEOPIXEL 0
  #define HAS_TWAI 0
  #define HAS_M5_UI 1
  #define SD_MISO -1
  #define SD_MOSI -1
  #define SD_SCLK -1
  #define SD_CS -1
#else
  #error "Please select one board at the top (BOARD_LILYGO / BOARD_WAVESHARE / BOARD_M5STACK)"
#endif

#if HAS_M5_UI
  #include <M5Unified.h>
  #include <mcp_can.h>
#endif
#if HAS_NEOPIXEL
  #include <Adafruit_NeoPixel.h>
#endif
#if HAS_TWAI
  #include <driver/twai.h>
#endif

#ifdef MODE_SMART_WIFI
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <WiFiManager.h>
  #include <PubSubClient.h>
#endif

#if HAS_NEOPIXEL
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
#if defined(BOARD_M5STACK)
MCP_CAN CAN0(CAN_CS_PIN);
#endif
// UART2 matches LilyGO/M5 examples (avoids UART1 flash-pin conflicts)
HardwareSerial RS485(2);
Preferences preferences;
#if HAS_M5_UI
int g_ui_page = 0;
unsigned long last_ui_time = 0;
#endif

void setLed(int r, int g, int b);
void addToLog(String msg, bool error);
int tbSelectSocValue(int soc_calc, int soc_raw, bool raw_valid, bool extended_layout);
#ifdef MODE_SMART_WIFI
void mqttReconnect();
#endif

#ifdef MODE_SMART_WIFI
  WebServer server(80);
  WiFiClient espClient;
  PubSubClient mqtt(espClient);
  Preferences histStore;
  
  String g_mqtt_server = ""; int g_mqtt_port = 1883; String g_mqtt_user = ""; String g_mqtt_pass = "";
  bool g_mqtt_enable = false; bool g_mqtt_full = false; bool g_ha_enable = false;
  bool g_sd_enable = false; bool g_sd_spy = false; bool g_serial_debug = false;
  String debug_log = ""; String debug_can_status = "Init";
  bool sd_ok = false; bool sd_error_flag = false;
  int g_theme_id = 0; String g_ntp_server = "pool.ntp.org"; int g_timezone_offset = 1;
  int16_t powerHistory[HISTORY_LEN]; int historyIdx = 0; 
  float daily_in[7]; float daily_out[7];
  unsigned long last_energy_calc = 0; unsigned long last_flash_save = 0;
  unsigned long last_hist_time = 0; unsigned long last_sd_time = 0; unsigned long last_mqtt_time = 0;
  int current_day = -1;
#endif

int g_bms_count = 2; int g_force_cell_count = 0; int g_detected_cells = 0; 
bool g_victron_enable = true;
float g_charge_amps = 30.0; float g_discharge_amps = 30.0;
float g_cvl_voltage = 52.5; float g_safe_volt = 53.25; float g_safe_cell = 3.55; float g_safe_drift = 0.20;
// Thermostat V142
float g_tc_min = 5.0; float g_tc_max = 50.0; float g_td_min = -20.0; float g_td_max = 60.0; int g_temp_mode = 0; 
int g_can_protocol = 0; bool g_expert_mode = true; bool g_easy_mode = false; bool g_setup_done = true; bool g_easy_auto_applied = false;
int g_soc_source_mode = 2; // 0=calc(rem/full), 1=raw(frame), 2=hybrid
bool g_maint_charge_mode = false;
float g_maint_target_v = 51.00;
bool g_auto_balance_enable = true;
uint32_t g_auto_balance_last_ts = 0;
bool g_runtime_auto_balance_due = false;
bool g_runtime_balance_reached_prev = false;
float factor_charge = 1.0; float factor_discharge = 1.0;

unsigned long last_poll_time = 0; unsigned long last_can_time = 0;
unsigned long stat_rx_count = 0; unsigned long stat_tx_count = 0;
bool can_error_flag = false; uint8_t alarm_flags = 0; String sys_error_msg = "OK"; bool simulation_active = false;
volatile bool g_rs485_service_busy = false;
unsigned long last_alarm_poll_time = 0;
int next_alarm_poll_bms = 0;
unsigned long last_sysparam_poll_time = 0;
int next_sysparam_poll_bms = 0;

struct BMSData {
  bool valid; float voltage; float current; int soc; int soh; float rem_ah; float full_ah; float cells[32];
  int cell_count; float temps[8]; int temp_count;
  float minCellV; float maxCellV; float avgCellV;
  int minCellIdx; int maxCellIdx; float maxTemp; float avgTemp; unsigned long last_seen;
};
BMSData bms[MAX_BMS];

struct VictronType {
  float totalCurrent; float avgVoltage; float avgSOC; float avgSOH; float maxChargeCurrent; float maxDischargeCurrent;
  float avgTemp; float totalCapacity; float remainCapacity; float totalPower; int activePacks;
} victronData;

struct TBFrame {
  bool valid;
  bool info_ascii_raw;
  uint8_t version;
  uint8_t adr;
  uint8_t cid1;
  uint8_t cid2;
  uint16_t length_field;
  uint16_t lenid;
  uint16_t checksum;
  uint8_t info[TB_INFO_MAX];
  size_t info_len;
};

struct TBDate {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

struct TBSysParam {
  float cell_high_v;
  float cell_low_v;
  float cell_under_v;
  float charge_high_t;
  float charge_low_t;
  float charge_current_max_a;
  float module_high_v;
  float module_low_v;
  float module_under_v;
  float discharge_high_t;
  float discharge_low_t;
  float discharge_current_max_a;
};

struct TBAlarmInfoParsed {
  uint8_t cell_count;
  uint8_t temp_count;
  uint8_t cell_voltage_status[16];
  uint8_t cell_temp_status[16];
  uint8_t balancer_temp_status;
  uint8_t environment_temp_status;
  uint8_t mosfet_temp_status;
  uint8_t charge_current_status;
  uint8_t module_voltage_status;
  uint8_t status_count;
  uint64_t status_bits;
};

struct TBAnalogDiag {
  uint8_t id;
  uint8_t cell_count;
  uint8_t temp_count;
  float min_cell_v;
  float max_cell_v;
  float current_a;
  float pack_v;
  float rem_ah;
  float full_ah;
  int soc;
  int soh;
};

uint64_t g_bms_alarm_bits[MAX_BMS];
unsigned long g_bms_alarm_last_seen[MAX_BMS];
uint8_t g_bms_alarm_status_count[MAX_BMS];
float g_bms_sys_module_high_v[MAX_BMS];
float g_bms_sys_module_under_v[MAX_BMS];
float g_bms_sys_charge_max_a[MAX_BMS];
float g_bms_sys_discharge_max_a[MAX_BMS];
float g_bms_sys_cell_high_v[MAX_BMS];
float g_bms_sys_charge_low_t[MAX_BMS];
float g_bms_sys_charge_high_t[MAX_BMS];
float g_bms_sys_discharge_low_t[MAX_BMS];
float g_bms_sys_discharge_high_t[MAX_BMS];
unsigned long g_bms_sys_last_seen[MAX_BMS];
float g_runtime_cvl = 0.0f;
float g_runtime_proto_ccl_cap = 0.0f;
float g_runtime_proto_dcl_cap = 0.0f;
bool g_runtime_proto_uv_hit = false;

// --- HELPERS ---
void setLed(int r, int g, int b) {
#if HAS_NEOPIXEL
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
#else
  (void)r; (void)g; (void)b;
#endif
}

#if HAS_M5_UI
void initM5Ui() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("Topband GW");
  M5.Display.setTextSize(1);
  M5.Display.printf("V%s M5+COMMU\n", FW_VERSION);
}

void drawM5Ui() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 4);
  if (g_ui_page == 0) M5.Display.print("Overview");
  else if (g_ui_page == 1) M5.Display.print("Packs");
  else M5.Display.print("Network");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (g_ui_page == 0) {
    M5.Display.setCursor(6, 36);
    M5.Display.printf("SOC  %.0f %%   SOH %.0f %%", victronData.avgSOC, victronData.avgSOH);
    M5.Display.setCursor(6, 56);
    M5.Display.printf("U    %.2f V", victronData.avgVoltage);
    M5.Display.setCursor(6, 76);
    M5.Display.printf("I    %.1f A", victronData.totalCurrent);
    M5.Display.setCursor(6, 96);
    M5.Display.printf("P    %.0f W", victronData.totalPower);
    M5.Display.setCursor(6, 116);
    M5.Display.printf("Packs %d / %d", victronData.activePacks, g_bms_count);
    M5.Display.setCursor(6, 136);
    M5.Display.setTextColor(can_error_flag ? TFT_RED : TFT_GREEN, TFT_BLACK);
#ifdef MODE_SMART_WIFI
    M5.Display.printf("CAN  %s", debug_can_status.c_str());
#else
    M5.Display.printf("CAN  %s", can_error_flag ? "ERR" : "OK");
#endif
  } else if (g_ui_page == 1) {
    int y = 32;
    for (int i = 0; i < g_bms_count && i < 8; i++) {
      M5.Display.setCursor(6, y);
      if (bms[i].valid)
        M5.Display.printf("B%d %3d%% %5.1fV %5.1fA", i, bms[i].soc, bms[i].voltage, bms[i].current);
      else
        M5.Display.printf("B%d  ---", i);
      y += 18;
    }
  } else {
    M5.Display.setCursor(6, 36);
#ifdef MODE_SMART_WIFI
    M5.Display.printf("WiFi %s", WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
    M5.Display.setCursor(6, 56);
    M5.Display.printf("IP   %s", WiFi.localIP().toString().c_str());
#else
    M5.Display.print("WiFi STEALTH");
#endif
    M5.Display.setCursor(6, 76);
    M5.Display.printf("Host %s", HOSTNAME);
    M5.Display.setCursor(6, 96);
    M5.Display.printf("FW   %s", FW_VERSION);
    M5.Display.setCursor(6, 116);
    M5.Display.printf("TX %lu  RX %lu", stat_tx_count, stat_rx_count);
  }
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(6, 220);
  M5.Display.print("A:Page  B:Prev  C:Sim");
}

void handleM5Buttons() {
  M5.update();
  if (M5.BtnA.wasPressed()) { g_ui_page = (g_ui_page + 1) % 3; drawM5Ui(); }
  if (M5.BtnB.wasPressed()) { g_ui_page = (g_ui_page + 2) % 3; drawM5Ui(); }
  if (M5.BtnC.wasPressed()) { simulation_active = !simulation_active; drawM5Ui(); }
}
#endif

bool initCanBus() {
#if defined(BOARD_M5STACK)
  SPI.begin(CAN_SPI_SCK, CAN_SPI_MISO, CAN_SPI_MOSI, CAN_CS_PIN);
  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    CAN0.setMode(MCP_NORMAL);
    pinMode(CAN_INT_PIN, INPUT);
#ifdef MODE_SMART_WIFI
    debug_can_status = "CAN OK";
#endif
    Serial.println("MCP2515 CAN OK");
    return true;
  }
#ifdef MODE_SMART_WIFI
  debug_can_status = "INIT FAIL";
#endif
  can_error_flag = true;
  Serial.println("MCP2515 CAN FAIL");
  return false;
#elif HAS_TWAI
  twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_cfg = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_cfg, &t_cfg, &f_cfg) == ESP_OK) {
    twai_start();
#ifdef MODE_SMART_WIFI
    debug_can_status = "CAN OK";
#endif
    Serial.println("TWAI CAN OK");
    return true;
  }
#ifdef MODE_SMART_WIFI
  debug_can_status = "INIT FAIL";
#endif
  can_error_flag = true;
  Serial.println("TWAI CAN FAIL");
  return false;
#else
  return false;
#endif
}

uint16_t get_u16(const uint8_t *b, int o) { return (b[o] << 8) | b[o + 1]; }
int16_t get_s16(const uint8_t *b, int o) { uint16_t v = get_u16(b, o); return (v > 32767) ? (v - 65536) : v; }
uint8_t parse_hex(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return 0xFF; }

uint16_t tbBuildLengthField(uint16_t lenid) {
  lenid &= 0x0FFF;
  uint16_t sum = ((lenid >> 8) & 0xF) + ((lenid >> 4) & 0xF) + (lenid & 0xF);
  uint16_t lchk = ((~(sum & 0xF)) + 1) & 0xF;
  return (lchk << 12) | lenid;
}

bool tbCheckLengthField(uint16_t length_field) {
  uint16_t lenid = length_field & 0x0FFF;
  uint16_t lchk = (length_field >> 12) & 0xF;
  uint16_t sum = ((lenid >> 8) & 0xF) + ((lenid >> 4) & 0xF) + (lenid & 0xF);
  uint16_t expected = ((~(sum & 0xF)) + 1) & 0xF;
  return lchk == expected;
}

String toHex2(uint8_t v) {
  char b[3];
  snprintf(b, sizeof(b), "%02X", v);
  return String(b);
}

String toHex4(uint16_t v) {
  char b[5];
  snprintf(b, sizeof(b), "%04X", v);
  return String(b);
}

bool parseHexByteAt(const String &s, int pos, uint8_t &out) {
  if (pos < 0 || pos + 1 >= s.length()) return false;
  uint8_t hi = parse_hex(s[pos]);
  uint8_t lo = parse_hex(s[pos + 1]);
  if (hi == 0xFF || lo == 0xFF) return false;
  out = (hi << 4) | lo;
  return true;
}

bool parseHexU16At(const String &s, int pos, uint16_t &out) {
  if (pos < 0 || pos + 3 >= s.length()) return false;
  uint8_t b0, b1;
  if (!parseHexByteAt(s, pos, b0)) return false;
  if (!parseHexByteAt(s, pos + 2, b1)) return false;
  out = ((uint16_t)b0 << 8) | b1;
  return true;
}

String tbBuildCommand(uint8_t addr, uint8_t cid2, const uint8_t *info, size_t info_len_bytes) {
  uint16_t lenid = (uint16_t)(info_len_bytes * 2);
  uint16_t length_field = tbBuildLengthField(lenid);

  String body = "";
  body.reserve(32 + lenid);
  body += toHex2(0x21);  // protocol version
  body += toHex2(addr);
  body += toHex2(0x46);  // CID1 battery
  body += toHex2(cid2);
  body += toHex4(length_field);
  for (size_t i = 0; i < info_len_bytes; i++) body += toHex2(info[i]);

  uint16_t sum = 0;
  for (int i = 0; i < body.length(); i++) sum += (uint8_t)body[i];
  uint16_t chk = ((~(sum & 0xFFFF)) + 1) & 0xFFFF;

  String cmd = "~";
  cmd += body;
  cmd += toHex4(chk);
  cmd += "\r";
  return cmd;
}

bool rs485ExchangeFrame(const String &cmd, String &resp, uint32_t timeout_ms) {
  if (g_rs485_service_busy) return false;
  g_rs485_service_busy = true;

  bool ok = false;
  resp = "";
  resp.reserve(512);

  #if defined(BOARD_WAVESHARE)
    digitalWrite(PIN_RS485_DIR, HIGH);
    delayMicroseconds(100);
  #endif
  while (RS485.available()) RS485.read();
  RS485.print(cmd);
  RS485.flush();
  #if defined(BOARD_WAVESHARE)
    delayMicroseconds(100);
    digitalWrite(PIN_RS485_DIR, LOW);
  #else
    // LilyGo MAX13487E autodirection + M5 COMMU SP3485 settle after TX
    delay(5);
  #endif

  unsigned long start = millis();
  unsigned long last_byte = 0;
  bool got = false;
  while (millis() - start < timeout_ms) {
    if (RS485.available()) {
      char c = (char)RS485.read();
      got = true;
      last_byte = millis();
      if (resp.length() < 700) resp += c;
      if (c == '\r' || c == '\n') { ok = true; break; }
    } else if (got && (millis() - last_byte > 80)) {
      ok = resp.length() > 0;
      break;
    } else {
      delay(1);
      yield();
    }
  }

  g_rs485_service_busy = false;
  return ok && resp.length() > 0;
}

bool tbDecodeFrame(const String &raw_resp, TBFrame &frame, String &err) {
  frame = {};
  err = "";
  int soi = raw_resp.indexOf('~');
  if (soi < 0) { err = "No SOI"; return false; }

  String s = raw_resp.substring(soi);
  int eoi = s.indexOf('\r');
  if (eoi < 0) eoi = s.indexOf('\n');
  if (eoi >= 0) s = s.substring(0, eoi + 1);

  if (s.length() < 18) { err = "Frame too short"; return false; }
  char eoi_ch = s[s.length() - 1];
  if (eoi_ch != '\r' && eoi_ch != '\n') { err = "No EOI"; return false; }

  uint8_t version, adr, cid1, cid2;
  uint16_t length_field, rx_chk;
  if (!parseHexByteAt(s, 1, version) || !parseHexByteAt(s, 3, adr) || !parseHexByteAt(s, 5, cid1) || !parseHexByteAt(s, 7, cid2)) {
    err = "Header hex parse failed"; return false;
  }
  if (!parseHexU16At(s, 9, length_field)) { err = "Length parse failed"; return false; }
  if (!tbCheckLengthField(length_field)) { err = "LCHKSUM invalid"; return false; }

  uint16_t lenid = length_field & 0x0FFF;
  int expected_len = 18 + lenid;
  if (s.length() < expected_len) { err = "Truncated frame"; return false; }
  if (!parseHexU16At(s, 13 + lenid, rx_chk)) { err = "CHKSUM parse failed"; return false; }

  uint16_t sum = 0;
  for (int i = 1; i < 13 + lenid; i++) sum += (uint8_t)s[i];
  uint16_t expected_chk = ((~(sum & 0xFFFF)) + 1) & 0xFFFF;
  if (rx_chk != expected_chk) { err = "CHKSUM invalid"; return false; }

  frame.valid = true;
  frame.version = version;
  frame.adr = adr;
  frame.cid1 = cid1;
  frame.cid2 = cid2;
  frame.length_field = length_field;
  frame.lenid = lenid;
  frame.checksum = rx_chk;
  frame.info_ascii_raw = false;

  if (lenid == 0) {
    frame.info_len = 0;
    return true;
  }
  if (lenid > TB_INFO_MAX) { err = "Info too long"; return false; }

  bool hex_ok = (lenid % 2) == 0;
  if (hex_ok) {
    size_t info_bytes = lenid / 2;
    for (size_t i = 0; i < info_bytes; i++) {
      uint8_t b;
      if (!parseHexByteAt(s, 13 + (int)(i * 2), b)) { hex_ok = false; break; }
      frame.info[i] = b;
    }
    if (hex_ok) {
      frame.info_len = info_bytes;
      return true;
    }
  }

  // Manufacturer info can be raw ASCII in protocol field.
  for (uint16_t i = 0; i < lenid; i++) frame.info[i] = (uint8_t)s[13 + i];
  frame.info_ascii_raw = true;
  frame.info_len = lenid;
  return true;
}

bool tbRequestFrame(uint8_t bms_id, uint8_t cid2, const uint8_t *info, size_t info_len_bytes, TBFrame &reply, String &err, bool allow_nonzero_cid2 = false) {
  String cmd = tbBuildCommand(bms_id, cid2, info, info_len_bytes);
  String raw;
  if (!rs485ExchangeFrame(cmd, raw, RS485_TIMEOUT + 200)) {
    err = "No response";
    return false;
  }
  if (!tbDecodeFrame(raw, reply, err)) return false;
  if (!allow_nonzero_cid2 && reply.cid2 != 0x00) {
    err = "BMS reply code 0x" + String(reply.cid2, HEX);
    return false;
  }
  return true;
}

float tbKelvin10ToC(uint16_t raw) {
  return ((float)raw - 2731.0f) / 10.0f;
}

bool tbParseDateFromFrame(const TBFrame &f, TBDate &d) {
  if (!f.valid || f.info_len < 7) return false;
  d.year = ((int)f.info[0] << 8) | (int)f.info[1];
  d.month = f.info[2];
  d.day = f.info[3];
  d.hour = f.info[4];
  d.minute = f.info[5];
  d.second = f.info[6];
  return true;
}

bool tbParseSystemParameterFromFrame(const TBFrame &f, TBSysParam &sp) {
  if (!f.valid || f.info_len < 24) return false;
  sp.cell_high_v = ((float)get_u16(f.info, 0)) / 1000.0f;
  sp.cell_low_v = ((float)get_u16(f.info, 2)) / 1000.0f;
  sp.cell_under_v = ((float)get_u16(f.info, 4)) / 1000.0f;
  sp.charge_high_t = tbKelvin10ToC(get_u16(f.info, 6));
  sp.charge_low_t = tbKelvin10ToC(get_u16(f.info, 8));
  sp.charge_current_max_a = ((float)((int16_t)get_u16(f.info, 10))) * 0.01f;
  sp.module_high_v = ((float)get_u16(f.info, 12)) / 100.0f;
  sp.module_low_v = ((float)get_u16(f.info, 14)) / 100.0f;
  sp.module_under_v = ((float)get_u16(f.info, 16)) / 100.0f;
  sp.discharge_high_t = tbKelvin10ToC(get_u16(f.info, 18));
  sp.discharge_low_t = tbKelvin10ToC(get_u16(f.info, 20));
  sp.discharge_current_max_a = ((float)((int16_t)get_u16(f.info, 22))) * 0.01f;
  return true;
}

bool tbParseAlarmInfoFromFrame(const TBFrame &f, TBAlarmInfoParsed &a) {
  if (!f.valid || f.info_len < 8) return false;
  memset(&a, 0, sizeof(a));

  size_t off = 1;
  if (off >= f.info_len) return false;
  a.cell_count = f.info[off];
  if (a.cell_count > 16) a.cell_count = 16;
  off += 1;
  if (off + a.cell_count > f.info_len) return false;
  for (uint8_t i = 0; i < a.cell_count; i++) a.cell_voltage_status[i] = f.info[off + i];
  off += a.cell_count;

  if (off >= f.info_len) return false;
  a.temp_count = f.info[off];
  off += 1;
  uint8_t cell_temp_bytes = (a.temp_count >= 3) ? (a.temp_count - 3) : 0;
  if (cell_temp_bytes > 16) cell_temp_bytes = 16;
  if (off + cell_temp_bytes > f.info_len) return false;
  for (uint8_t i = 0; i < cell_temp_bytes; i++) a.cell_temp_status[i] = f.info[off + i];
  off += cell_temp_bytes;

  if (off + 3 > f.info_len) return false;
  a.balancer_temp_status = f.info[off];
  a.environment_temp_status = f.info[off + 1];
  a.mosfet_temp_status = f.info[off + 2];
  off += 3;

  if (off + 3 > f.info_len) return false;
  a.charge_current_status = f.info[off];
  a.module_voltage_status = f.info[off + 1];
  a.status_count = f.info[off + 2];
  off += 3;

  if (off + a.status_count > f.info_len) return false;
  uint8_t status_bytes = a.status_count > 8 ? 8 : a.status_count;
  for (uint8_t i = 0; i < status_bytes; i++) a.status_bits |= ((uint64_t)f.info[off + i]) << (8 * i);
  return true;
}

bool tbParseAnalogDiagFromFrame(const TBFrame &f, TBAnalogDiag &a) {
  if (!f.valid || f.info_len < 16) return false;
  memset(&a, 0, sizeof(a));
  size_t p = 0;
  a.id = f.info[p++];
  a.cell_count = f.info[p++];
  if (a.cell_count > 32) a.cell_count = 32;

  float min_v = 99.0f, max_v = 0.0f;
  for (uint8_t i = 0; i < a.cell_count; i++) {
    if (p + 1 >= f.info_len) return false;
    float v = ((float)get_u16(f.info, (int)p)) / 1000.0f;
    if (v > 0.1f) {
      if (v < min_v) min_v = v;
      if (v > max_v) max_v = v;
    }
    p += 2;
  }
  a.min_cell_v = min_v < 90.0f ? min_v : 0.0f;
  a.max_cell_v = max_v;

  if (p >= f.info_len) return false;
  a.temp_count = f.info[p++];
  if (a.temp_count > 8) a.temp_count = 8;
  size_t temp_bytes = (size_t)a.temp_count * 2;
  if (p + temp_bytes > f.info_len) return false;
  p += temp_bytes;

  if (p + 10 >= f.info_len) return false;
  a.current_a = ((float)((int16_t)get_u16(f.info, (int)p))) * 0.01f; p += 2;
  a.pack_v = ((float)get_u16(f.info, (int)p)) * 0.01f; p += 2;
  a.rem_ah = ((float)get_u16(f.info, (int)p)) * 0.01f; p += 3;
  a.full_ah = ((float)get_u16(f.info, (int)p)) * 0.01f; p += 2;

  int soc_calc = 0;
  if (a.full_ah > 0.01f) soc_calc = (int)((a.rem_ah / a.full_ah) * 100.0f);
  if (soc_calc > 100) soc_calc = 100;
  if (soc_calc < 0) soc_calc = 0;
  a.soc = soc_calc;
  a.soh = 100;

  // Newer Topband payloads carry cycle(2B), SOC(1B), SOH(1B).
  // Keep legacy fallback to avoid regressions with older frames.
  size_t meta = p;
  if (meta + 3 < f.info_len) {
    uint8_t raw_soc = f.info[meta + 2];
    uint8_t raw_soh = f.info[meta + 3];
    bool extended_layout = (meta + 7 < f.info_len);
    bool raw_valid = (raw_soc <= 100) && (raw_soh <= 100);
    a.soc = tbSelectSocValue(soc_calc, (int)raw_soc, raw_valid, extended_layout);
    if (raw_valid && (extended_layout || g_soc_source_mode == 1 || abs((int)raw_soc - soc_calc) <= 15)) {
      a.soh = (int)raw_soh;
    } else {
      uint16_t s16 = get_u16(f.info, (int)(meta + 2));
      if (s16 <= 100) a.soh = (int)s16;
    }
  }
  return true;
}

const char* tbAlarmBitName(uint8_t bit) {
  switch (bit) {
    case 0: return "Cell over voltage protect";
    case 1: return "Cell under voltage";
    case 2: return "Charge over current protect";
    case 3: return "Cell over voltage alarm";
    case 4: return "Discharge over current 1 protect";
    case 5: return "Cell temperature discharge protect";
    case 6: return "Cell temperature charge protect";
    case 7: return "Pack under voltage alarm";
    case 8: return "Open current limit";
    case 9: return "Charge MOSFET on";
    case 10: return "Discharge MOSFET on";
    case 11: return "Short circuit protect";
    case 12: return "Cell under voltage protect";
    case 13: return "Pack under voltage protect";
    case 14: return "Reverse protect";
    case 15: return "SOC low alarm";
    case 20: return "Charger connected";
    case 22: return "Discharging";
    case 23: return "Charging";
    case 28: return "Cell low force protect";
    case 40: return "Pack over voltage alarm";
    case 41: return "MOS NTC temperature alarm";
    case 42: return "Environment temperature low alarm";
    case 43: return "Environment temperature high alarm";
    case 44: return "Cell temperature low alarm";
    case 45: return "Cell temperature high alarm";
    case 46: return "Discharge current alarm";
    case 47: return "Charge current alarm";
    case 48: return "Balance temperature alarm";
    case 49: return "Balance temperature protect";
    case 50: return "Discharge MOSFET fault";
    case 51: return "Charge MOSFET fault";
    case 52: return "Current sensor fault";
    case 53: return "AFE fault";
    case 54: return "NTC fault";
    case 55: return "Cell fault";
    case 56: return "Discharge over current 2 protect";
    case 58: return "Pack over voltage protect";
    case 59: return "MOS temperature protect";
    case 60: return "Discharge MOS forced close";
    case 61: return "Charge MOS forced close";
    case 62: return "Environment protect discharging";
    case 63: return "Environment protect charging";
    default: return "Alarm/Status";
  }
}

const uint64_t TB_CRITICAL_ALARM_MASK =
    (1ULL << 0)  | (1ULL << 2)  | (1ULL << 4)  | (1ULL << 5)  |
    (1ULL << 6)  | (1ULL << 11) | (1ULL << 12) | (1ULL << 13) |
    (1ULL << 50) | (1ULL << 51) | (1ULL << 52) | (1ULL << 53) |
    (1ULL << 54) | (1ULL << 55) | (1ULL << 58) | (1ULL << 59);

float tbUnderVoltSanityCap(int cells) {
  if (cells <= 0 || cells > 32) return 57.0f;
  float cap = ((float)cells) * 3.20f;
  if (cap < 40.0f) cap = 40.0f;
  if (cap > 57.0f) cap = 57.0f;
  return cap;
}

bool tbShouldFlagProtoUnderVolt(float pack_v, float module_under_v, int cells) {
  if (module_under_v <= 1.0f) return false;
  if (cells <= 0 || cells > 32) return false;
  float trigger_v = module_under_v + 0.05f;
  float sanity_cap = tbUnderVoltSanityCap(cells);
  if (trigger_v > sanity_cap) return false;
  return pack_v <= trigger_v;
}

float tbOverVoltCellSanityFloor(float cell_high_v) {
  float floor = cell_high_v - 0.12f;
  if (cell_high_v < 3.2f || cell_high_v > 4.5f) floor = 3.58f;
  if (floor < 3.50f) floor = 3.50f;
  if (floor > 4.05f) floor = 4.05f;
  return floor;
}

float tbOverVoltPackSanityFloor(float module_high_v, int cells) {
  float floor = module_high_v - 0.80f;
  if (module_high_v < 30.0f || module_high_v > 70.0f) {
    if (cells > 0 && cells <= 32) floor = ((float)cells) * 3.55f;
    else floor = 50.0f;
  }
  if (floor < 40.0f) floor = 40.0f;
  if (floor > 61.0f) floor = 61.0f;
  return floor;
}

uint64_t tbFilterCriticalAlarmBits(uint64_t status_bits, float pack_v, float max_temp_c, int cells, float temp_limit_c, float max_cell_v, float cell_high_v, float module_high_v) {
  uint64_t bits = status_bits & TB_CRITICAL_ALARM_MASK;
  const uint64_t uv_bits = (1ULL << 12) | (1ULL << 13);
  if (cells <= 0 || cells > 32) bits &= ~uv_bits;
  else if (pack_v > tbUnderVoltSanityCap(cells)) bits &= ~uv_bits;

  const uint64_t ov_bits = (1ULL << 0) | (1ULL << 58);
  float ov_cell_floor = tbOverVoltCellSanityFloor(cell_high_v);
  float ov_pack_floor = tbOverVoltPackSanityFloor(module_high_v, cells);
  if (max_cell_v < ov_cell_floor && pack_v < ov_pack_floor) bits &= ~ov_bits;

  const uint64_t temp_bits = (1ULL << 5) | (1ULL << 6) | (1ULL << 59);
  float temp_guard = temp_limit_c - 3.0f;
  if (temp_guard < 15.0f) temp_guard = 15.0f;
  if (max_temp_c < temp_guard) bits &= ~temp_bits;

  return bits;
}

bool tbHasCriticalAlarm(uint64_t status_bits) {
  return (status_bits & TB_CRITICAL_ALARM_MASK) != 0;
}

String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  const char *hexDigits = "0123456789ABCDEF";
  for (int i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out += hexDigits[(c >> 4) & 0x0F];
          out += hexDigits[c & 0x0F];
        } else {
          out += (char)c;
        }
        break;
    }
  }
  return out;
}

String u64Hex(uint64_t v) {
  char b[19];
  snprintf(b, sizeof(b), "%016llX", (unsigned long long)v);
  return String(b);
}

int tbSelectSocValue(int soc_calc, int soc_raw, bool raw_valid, bool extended_layout) {
  if (soc_calc < 0) soc_calc = 0;
  if (soc_calc > 100) soc_calc = 100;
  if (soc_raw < 0) soc_raw = 0;
  if (soc_raw > 100) soc_raw = 100;

  if (g_soc_source_mode == 0) return soc_calc;
  if (g_soc_source_mode == 1) return raw_valid ? soc_raw : soc_calc;

  if (!raw_valid) return soc_calc;
  if (extended_layout || abs(soc_raw - soc_calc) <= 15) return soc_raw;
  return soc_calc;
}

bool tbParseManufacturerFromFrame(const TBFrame &f, String &hw, String &sw, String &id) {
  if (!f.valid || f.info_len < 24) return false;
  String raw = "";
  raw.reserve(f.info_len + 1);
  for (size_t i = 0; i < f.info_len; i++) raw += (char)f.info[i];
  if (raw.length() < 24) return false;
  hw = raw.substring(0, 20); hw.trim();
  sw = raw.substring(20, 24); sw.trim();
  id = raw.substring(24); id.trim();
  return true;
}

bool tbParseHistoricalFirstFromFrame(const TBFrame &f, TBDate &d, int &event_type, uint64_t &status_bits, float &current_a, float &pack_v, float &remain_ah) {
  if (!f.valid || f.info_len < 24) return false;
  if (f.info_len < 11) return false;

  d.year = ((int)f.info[2] << 8) | (int)f.info[3];
  d.month = f.info[4];
  d.day = f.info[5];
  d.hour = f.info[6];
  d.minute = f.info[7];
  d.second = f.info[8];
  event_type = f.info[9];
  uint8_t status_count = f.info[10];
  status_bits = 0;
  size_t off = 11;
  uint8_t copy_n = status_count > 8 ? 8 : status_count;
  if (off + copy_n <= f.info_len) {
    for (uint8_t i = 0; i < copy_n; i++) status_bits |= ((uint64_t)f.info[off + i]) << (8 * i);
  }
  if (f.info_len >= 24) {
    current_a = ((float)((int16_t)get_u16(f.info, 18))) / 100.0f;
    pack_v = ((float)get_u16(f.info, 20)) / 100.0f;
    remain_ah = ((float)get_u16(f.info, 22)) / 100.0f;
  } else {
    current_a = 0; pack_v = 0; remain_ah = 0;
  }
  return true;
}

void tbBuildAutoCfgSuggestion(const TBSysParam &sp, float buf_cell_mv, float buf_pack_v, float buf_temp_c, float buf_curr_pct,
                              float &safe_cell, float &safe_volt, float &tc_min, float &tc_max, float &td_min, float &td_max,
                              float &chg_a, float &dis_a, float &cvl_v) {
  float buf_cell_v = buf_cell_mv / 1000.0f;
  safe_cell = sp.cell_high_v - buf_cell_v;
  safe_volt = sp.module_high_v - buf_pack_v;

  tc_min = sp.charge_low_t + buf_temp_c;
  tc_max = sp.charge_high_t - buf_temp_c;
  td_min = sp.discharge_low_t + buf_temp_c;
  td_max = sp.discharge_high_t - buf_temp_c;

  // Safety-first: auto profiles never allow charging below +5 C.
  if (tc_min < 5.0f) tc_min = 5.0f;
  if (tc_max > 50.0f) tc_max = 50.0f;
  if (td_min < -20.0f) td_min = -20.0f;
  if (td_max > 60.0f) td_max = 60.0f;

  float scale = 1.0f - (buf_curr_pct / 100.0f);
  if (scale < 0.1f) scale = 0.1f;
  chg_a = sp.charge_current_max_a * scale;
  dis_a = sp.discharge_current_max_a * scale;

  if (safe_cell < 3.2f) safe_cell = 3.2f;
  if (safe_cell > 3.6f) safe_cell = 3.6f;
  if (safe_volt < 40.0f) safe_volt = 40.0f;
  if (safe_volt > 60.0f) safe_volt = 60.0f;

  int est_cells = 0;
  if (sp.cell_high_v > 2.5f) {
    float ratio = sp.module_high_v / sp.cell_high_v;
    if (ratio >= 8.0f && ratio <= 24.0f) est_cells = (int)(ratio + 0.5f);
  }
  if (est_cells > 0) {
    float pack_cap = safe_cell * (float)est_cells;
    if (safe_volt > pack_cap) safe_volt = pack_cap;
  }

  if (tc_min > tc_max - 1.0f) tc_min = tc_max - 1.0f;
  if (td_min > td_max - 1.0f) td_min = td_max - 1.0f;
  if (chg_a < 1.0f) chg_a = 1.0f;
  if (dis_a < 1.0f) dis_a = 1.0f;

  cvl_v = safe_volt - 0.20f;
  if (cvl_v < 48.0f) cvl_v = 48.0f;
  if (cvl_v > safe_volt) cvl_v = safe_volt;
}
bool tbSystemParamPlausible(const TBSysParam &sp) {
  if (sp.cell_high_v < 3.0f || sp.cell_high_v > 4.5f) return false;
  if (sp.cell_low_v < 2.0f || sp.cell_low_v > 3.6f) return false;
  if (sp.cell_under_v < 1.8f || sp.cell_under_v > 3.5f) return false;
  if (sp.module_high_v < 40.0f || sp.module_high_v > 65.0f) return false;
  if (sp.module_under_v < 20.0f || sp.module_under_v > 60.0f) return false;
  if (sp.module_under_v >= sp.module_high_v) return false;
  if (sp.charge_current_max_a < 1.0f || sp.charge_current_max_a > 500.0f) return false;
  if (sp.discharge_current_max_a < 1.0f || sp.discharge_current_max_a > 500.0f) return false;
  if (sp.charge_low_t < -40.0f || sp.charge_low_t > 40.0f) return false;
  if (sp.charge_high_t < 20.0f || sp.charge_high_t > 100.0f) return false;
  if (sp.discharge_low_t < -40.0f || sp.discharge_low_t > 40.0f) return false;
  if (sp.discharge_high_t < 20.0f || sp.discharge_high_t > 100.0f) return false;
  return true;
}

bool pollBmsAlarmStatus(int bms_id) {
  if (bms_id < 0 || bms_id >= MAX_BMS) return false;
  uint8_t req[1] = {(uint8_t)bms_id};
  TBFrame fr;
  String err;
  if (!tbRequestFrame((uint8_t)bms_id, 0x44, req, 1, fr, err)) return false;
  TBAlarmInfoParsed a;
  if (!tbParseAlarmInfoFromFrame(fr, a)) return false;
  g_bms_alarm_bits[bms_id] = a.status_bits;
  g_bms_alarm_status_count[bms_id] = a.status_count;
  g_bms_alarm_last_seen[bms_id] = millis();
  return true;
}

bool pollBmsSystemParameter(int bms_id) {
  if (bms_id < 0 || bms_id >= MAX_BMS) return false;
  TBFrame fr;
  String err;
  if (!tbRequestFrame((uint8_t)bms_id, 0x47, nullptr, 0, fr, err)) return false;
  TBSysParam sp;
  if (!tbParseSystemParameterFromFrame(fr, sp)) return false;
  if (!tbSystemParamPlausible(sp)) {
    addToLog("Spike Filter: SystemParam out of range BMS" + String(bms_id), true);
    return false;
  }
  bool prev_fresh = (g_bms_sys_last_seen[bms_id] > 0) && (millis() - g_bms_sys_last_seen[bms_id] < 300000);
  if (prev_fresh) {
    if (fabs(sp.module_high_v - g_bms_sys_module_high_v[bms_id]) > 5.0f) {
      addToLog("Spike Filter: module_high jump BMS" + String(bms_id), true);
      return false;
    }
    if (fabs(sp.charge_current_max_a - g_bms_sys_charge_max_a[bms_id]) > 100.0f) {
      addToLog("Spike Filter: charge_max jump BMS" + String(bms_id), true);
      return false;
    }
    if (fabs(sp.discharge_current_max_a - g_bms_sys_discharge_max_a[bms_id]) > 100.0f) {
      addToLog("Spike Filter: discharge_max jump BMS" + String(bms_id), true);
      return false;
    }
  }
  g_bms_sys_module_high_v[bms_id] = sp.module_high_v;
  g_bms_sys_module_under_v[bms_id] = sp.module_under_v;
  g_bms_sys_charge_max_a[bms_id] = sp.charge_current_max_a;
  g_bms_sys_discharge_max_a[bms_id] = sp.discharge_current_max_a;
  g_bms_sys_cell_high_v[bms_id] = sp.cell_high_v;
  g_bms_sys_charge_low_t[bms_id] = sp.charge_low_t;
  g_bms_sys_charge_high_t[bms_id] = sp.charge_high_t;
  g_bms_sys_discharge_low_t[bms_id] = sp.discharge_low_t;
  g_bms_sys_discharge_high_t[bms_id] = sp.discharge_high_t;
  g_bms_sys_last_seen[bms_id] = millis();
  return true;
}

void addToLog(String msg, bool error) {
  #ifdef MODE_SMART_WIFI
    String clr = error ? "log-err" : "log-ok";
    if (debug_log.length() > 6000) debug_log = debug_log.substring(0, 5000);
    String tm = String(millis()/1000); 
    debug_log = "<div class='" + clr + "'>[" + tm + "s] " + msg + "</div>" + debug_log;
    if(error || g_serial_debug) Serial.println(msg);
  #else
    if(error) Serial.println(msg);
  #endif
}

String getTimeStr() {
  struct tm t; if (!getLocalTime(&t)) return String(millis() / 1000) + "s";
  char b[20]; strftime(b, sizeof(b), "%H:%M:%S", &t); return String(b);
}

void handleLedStatus() {
  unsigned long m = millis();
  #ifdef MODE_SMART_WIFI
  if(WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) { 
      int val = (int)((sin(m / 300.0) + 1.0) * 60.0); setLed(val, 0, val); return; 
  }
  #endif
  if (victronData.activePacks == 0) {
      int val = (int)((sin(m / 200.0) + 1.0) * 100.0); setLed(0, 0, val); 
  } else if( (alarm_flags & 0x02) || (alarm_flags & 0x08) || (alarm_flags & 0x10) || (alarm_flags & 0x40) ) {
      bool on = (m / 80) % 2; setLed(on ? 255 : 0, 0, 0); 
  } else if (alarm_flags & 0x20) {
      bool on = (m / 500) % 2; setLed(on ? 255 : 0, on ? 160 : 0, 0); 
  } else {
      int val = (int)((sin(m / 1500.0) + 1.0) * 60.0); setLed(0, val, 0); 
  }
}

// --- AP MODE CALLBACK ---
#ifdef MODE_SMART_WIFI
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("AP Mode Active");
  setLed(255, 0, 0); // ROT
}
#endif

void parseTopband(String raw, int addr) {
  #ifdef MODE_SMART_WIFI
    if(g_serial_debug) { Serial.print("RX BMS "); Serial.print(addr); Serial.print(": "); Serial.println(raw); }
    if(g_sd_spy && sd_ok && HAS_SD_CARD) {
        File f = SD.open("/spy.txt", FILE_APPEND);
        if(f) { f.printf("[%s] BMS%d: %s\n", getTimeStr().c_str(), addr, raw.c_str()); f.close(); }
    }
  #endif
  if (raw.length() < 20) { addToLog("Err: Short Frame", true); return; }
  stat_rx_count++; uint8_t b[512]; int blen = 0;
  for (int i = 0; i < raw.length() - 1; i += 2) { if (blen >= 511) break; b[blen++] = (parse_hex(raw[i]) << 4) | parse_hex(raw[i + 1]); }
  int idx = -1; for (int i = 0; i < blen - 1; i++) { if (b[i] == 0xD0 && b[i + 1] == 0x7C) { idx = i; break; } }
  if (idx == -1) return;
  int p = idx + 2; p++; int cells = b[p++];
  if (g_force_cell_count > 0) cells = g_force_cell_count;
  if (cells > 32) cells = 32;
  if (cells < 0) cells = 0;
  g_detected_cells = cells;
  float vSum = 0, minV = 99, maxV = 0; int minI = 0, maxI = 0;
  for (int i = 0; i < cells; i++) {
    if (p + 1 >= blen) return;
    float v = get_u16(b, p) / 1000.0; bms[addr].cells[i] = v;
    if (v > 0.1) { vSum += v; if (v < minV) { minV = v; minI = i + 1; } if (v > maxV) { maxV = v; maxI = i + 1; } }
    p += 2;
  }
  bms[addr].cell_count = cells; bms[addr].minCellV = minV; bms[addr].maxCellV = maxV;
  bms[addr].minCellIdx = minI; bms[addr].maxCellIdx = maxI; bms[addr].avgCellV = (cells > 0) ? vSum / cells : 0;
  if (p < blen) {
    int t_cnt = b[p++]; if (t_cnt > 8) t_cnt = 8; bms[addr].temp_count = t_cnt;
    float max_t = -99; for (int i = 0; i < t_cnt; i++) {
      if (p + 1 >= blen) break; float t = (get_u16(b, p) - 2731) / 10.0;
      if (t < -50 || t > 150) t = 25.0; bms[addr].temps[i] = t; if (t > max_t) max_t = t; p += 2;
    }
    bms[addr].maxTemp = max_t; bms[addr].avgTemp = max_t; 
  }
  if (p + 10 < blen) {
    float cur = get_s16(b, p) * 0.01; p += 2; float volt = get_u16(b, p) * 0.01; p += 2; float rem = get_u16(b, p) * 0.01; p += 3; float full = get_u16(b, p) * 0.01; p += 2;
    float v_min = (cells > 0) ? (cells * 2.0f) : 20.0f;
    float v_max = (cells > 0) ? (cells * 4.05f) : 65.0f;
    if (volt < v_min || volt > v_max || abs(cur) > 500 || rem < 0 || full < 0 || full > 2000) { addToLog("Bad Main Data", true); return; }
    int new_soc_calc = (full > 0) ? (int)((rem / full) * 100.0) : 0; if (new_soc_calc > 100) new_soc_calc = 100; if (new_soc_calc < 0) new_soc_calc = 0;
    int new_soc = new_soc_calc;
    int new_soh = 100;

    // Newer payload variant: cycle(2B), SOC(1B), SOH(1B) after full_ah.
    // Accept only if plausible against calculated SOC, then fallback to legacy parse.
    int meta = p;
    if (meta + 3 < blen) {
      uint8_t raw_soc = b[meta + 2];
      uint8_t raw_soh = b[meta + 3];
      bool extended_layout = (meta + 7 < blen);
      bool raw_valid = (raw_soc <= 100) && (raw_soh <= 100);
      new_soc = tbSelectSocValue(new_soc_calc, (int)raw_soc, raw_valid, extended_layout);
      if (raw_valid && (extended_layout || g_soc_source_mode == 1 || abs((int)raw_soc - new_soc_calc) <= 15)) {
        new_soh = (int)raw_soh;
      } else {
        uint16_t s16 = get_u16(b, meta + 2);
        if (s16 <= 100) new_soh = (int)s16;
      }
    }

    bool prev_valid = bms[addr].valid && (millis() - bms[addr].last_seen < 15000);
    if (prev_valid) {
      if (fabs(volt - bms[addr].voltage) > 5.0f) { addToLog("Spike Filter: Voltage jump BMS" + String(addr), true); return; }
      if (fabs(cur - bms[addr].current) > 250.0f) { addToLog("Spike Filter: Current jump BMS" + String(addr), true); return; }
      if (abs(new_soc - bms[addr].soc) > 20) { addToLog("Spike Filter: SOC jump BMS" + String(addr), true); return; }
    }

    bms[addr].current = cur; bms[addr].voltage = volt; bms[addr].rem_ah = rem; bms[addr].full_ah = full;
    bms[addr].soc = new_soc;
    bms[addr].soh = new_soh;
  }
  bms[addr].valid = true; bms[addr].last_seen = millis();
}

float calcFactor(float t, float min, float max) {
    if (t < min) return 0.0; if (t > max) return 0.0;
    float soft_zone = 5.0; 
    if (t < (min + soft_zone)) return 0.2; 
    if (t > (max - soft_zone)) return 0.5; 
    return 1.0; 
}

float getEffectiveChargeMinTemp() {
  if (g_expert_mode) return g_tc_min;
  return (g_tc_min < 5.0f) ? 5.0f : g_tc_min;
}

float getEffectiveDischargeMinTemp() {
  if (g_expert_mode) return g_td_min;
  return (g_td_min < -20.0f) ? -20.0f : g_td_min;
}
void enforceCellBasedSafetyCaps() {
  int cells = g_force_cell_count > 0 ? g_force_cell_count : g_detected_cells;
  if (cells <= 0 || cells > 32) return;

  float cap_cvl = ((float)cells) * 3.50f;
  float cap_safe = ((float)cells) * 3.55f;
  float cap_cell = 3.55f;

  if (g_cvl_voltage > cap_cvl) g_cvl_voltage = cap_cvl;
  if (g_safe_volt > cap_safe) g_safe_volt = cap_safe;
  if (g_safe_cell > cap_cell) g_safe_cell = cap_cell;
  if (g_maint_target_v > g_safe_volt) g_maint_target_v = g_safe_volt;
}

void saveAutoBalanceTimestamp(uint32_t ts) {
  preferences.begin("gateway", false);
  preferences.putULong("ab_last", ts);
  preferences.end();
}

void calculateVictronData() {
  float sumI = 0, sumV = 0, sumSOC = 0, sumSOH = 0, sumT = 0, sumCap = 0, sumRem = 0;
  float proto_ccl_cap = 0, proto_dcl_cap = 0, proto_cvl_cap = 999.0f;
  bool proto_under_v_hit = false;
  int first_proto_idx = -1;
  int proto_count = 0;
  int count = 0; alarm_flags = 0; sys_error_msg = "OK";
  #ifdef MODE_SIMPLE_CABLE
    if(g_detected_cells > 0) { g_cvl_voltage = g_detected_cells * 3.5; g_safe_volt = g_detected_cells * 3.65; }
  #endif
  enforceCellBasedSafetyCaps();

  float t_check_val = 25.0; 
  for (int i = 0; i < g_bms_count; i++) {
    bool on = bms[i].valid && (millis() - bms[i].last_seen < 60000);
    if (simulation_active) on = true;
    if (on) {
      bool alarm_fresh = (g_bms_alarm_last_seen[i] > 0) && (millis() - g_bms_alarm_last_seen[i] < 60000);
      if (alarm_fresh) {
        int cells_for_filter = (bms[i].cell_count > 0) ? bms[i].cell_count : g_detected_cells;
        float temp_limit = (g_tc_max < g_td_max) ? g_tc_max : g_td_max;
        uint64_t crit_bits = tbFilterCriticalAlarmBits(
            g_bms_alarm_bits[i],
            bms[i].voltage,
            bms[i].maxTemp,
            cells_for_filter,
            temp_limit,
            bms[i].maxCellV,
            g_bms_sys_cell_high_v[i],
            g_bms_sys_module_high_v[i]);
        if (crit_bits != 0) {
          const uint64_t uv_alarm_bits = (1ULL << 12) | (1ULL << 13);
          if (crit_bits & uv_alarm_bits) {
            alarm_flags |= 0x10;
            if (sys_error_msg == "OK") sys_error_msg = "ALARM: PACK UNDERVOLT";
          } else {
            alarm_flags |= 0x40;
            if (sys_error_msg == "OK") sys_error_msg = "ALARM: BMS STATUS";
          }
        }
      }
      bool sp_fresh = (g_bms_sys_last_seen[i] > 0) && (millis() - g_bms_sys_last_seen[i] < 300000);
      if (sp_fresh) {
        if (first_proto_idx < 0) first_proto_idx = i;
        if (g_bms_sys_charge_max_a[i] > 0.1f) proto_ccl_cap += g_bms_sys_charge_max_a[i];
        if (g_bms_sys_discharge_max_a[i] > 0.1f) proto_dcl_cap += g_bms_sys_discharge_max_a[i];
        int cells_for_cap = (bms[i].cell_count > 0) ? bms[i].cell_count : g_detected_cells;
        float cvl_cap = g_bms_sys_module_high_v[i] - 0.20f;
        if (cells_for_cap > 0 && cells_for_cap <= 32) {
          float chem_cap = ((float)cells_for_cap) * 3.60f;
          if (cvl_cap > chem_cap) cvl_cap = chem_cap;
        }
        if (g_safe_volt > 40.0f && cvl_cap > (g_safe_volt - 0.20f)) cvl_cap = g_safe_volt - 0.20f;
        if (g_cvl_voltage > 40.0f && cvl_cap > g_cvl_voltage) cvl_cap = g_cvl_voltage;
        if (cvl_cap > 40.0f && cvl_cap < proto_cvl_cap) proto_cvl_cap = cvl_cap;
        int cells_for_uv = (bms[i].cell_count > 0) ? bms[i].cell_count : g_detected_cells;
        if (tbShouldFlagProtoUnderVolt(bms[i].voltage, g_bms_sys_module_under_v[i], cells_for_uv)) proto_under_v_hit = true;
        proto_count++;
      }
      if(bms[i].voltage > g_safe_volt) { alarm_flags |= 0x02; sys_error_msg = "ALARM: PACK OVERVOLT"; }
      if(bms[i].maxCellV > g_safe_cell) { alarm_flags |= 0x02; sys_error_msg = "ALARM: CELL OVERVOLT"; }
      if(g_temp_mode == 0) t_check_val = bms[i].maxTemp; else t_check_val = bms[i].avgTemp; 
      if((bms[i].maxCellV - bms[i].minCellV) > g_safe_drift) { alarm_flags |= 0x20; if(sys_error_msg == "OK") sys_error_msg = "WARN: CELL IMBALANCE"; }
      sumI += bms[i].current; sumV += bms[i].voltage; sumSOC += bms[i].soc; sumSOH += bms[i].soh; sumT += bms[i].avgTemp; sumCap += bms[i].full_ah; sumRem += bms[i].rem_ah; count++;
    }
  }
  
  if (count > 0) {
    if (g_easy_mode && !g_expert_mode && !g_easy_auto_applied && first_proto_idx >= 0) {
      TBSysParam sp;
      sp.cell_high_v = g_bms_sys_cell_high_v[first_proto_idx];
      sp.module_high_v = g_bms_sys_module_high_v[first_proto_idx];
      sp.charge_low_t = g_bms_sys_charge_low_t[first_proto_idx];
      sp.charge_high_t = g_bms_sys_charge_high_t[first_proto_idx];
      sp.discharge_low_t = g_bms_sys_discharge_low_t[first_proto_idx];
      sp.discharge_high_t = g_bms_sys_discharge_high_t[first_proto_idx];
      sp.charge_current_max_a = g_bms_sys_charge_max_a[first_proto_idx];
      sp.discharge_current_max_a = g_bms_sys_discharge_max_a[first_proto_idx];

      float n_safe_cell, n_safe_volt, n_tc_min, n_tc_max, n_td_min, n_td_max, n_chg_a, n_dis_a, n_cvl_v;
      tbBuildAutoCfgSuggestion(sp, 20.0f, 0.30f, 2.0f, 10.0f,
                               n_safe_cell, n_safe_volt, n_tc_min, n_tc_max, n_td_min, n_td_max, n_chg_a, n_dis_a, n_cvl_v);
      g_safe_cell = n_safe_cell;
      g_safe_volt = n_safe_volt;
      g_tc_min = n_tc_min;
      g_tc_max = n_tc_max;
      g_td_min = n_td_min;
      g_td_max = n_td_max;
      g_charge_amps = n_chg_a;
      g_discharge_amps = n_dis_a;
      g_cvl_voltage = n_cvl_v;
      g_easy_auto_applied = true;
      addToLog("Easy Mode: Auto config applied from BMS system params", false);
    }

    victronData.activePacks = count; victronData.totalCurrent = sumI; victronData.avgVoltage = sumV / count;
    victronData.totalPower = sumI * victronData.avgVoltage; victronData.avgSOC = sumSOC / count;
    victronData.avgSOH = sumSOH / count; victronData.avgTemp = sumT / count; victronData.totalCapacity = sumCap; victronData.remainCapacity = sumRem;
    
    float eff_tc_min = getEffectiveChargeMinTemp();
    float eff_td_min = getEffectiveDischargeMinTemp();
    factor_charge = calcFactor(t_check_val, eff_tc_min, g_tc_max);
    factor_discharge = calcFactor(t_check_val, eff_td_min, g_td_max);

    float safe_chg = count * g_charge_amps * factor_charge;
    float safe_dis = count * g_discharge_amps * factor_discharge;

    if (proto_count > 0) {
      if (proto_ccl_cap > 0.1f && safe_chg > proto_ccl_cap) safe_chg = proto_ccl_cap;
      if (proto_dcl_cap > 0.1f && safe_dis > proto_dcl_cap) safe_dis = proto_dcl_cap;
      if (proto_cvl_cap < 900.0f) g_runtime_cvl = proto_cvl_cap;
      else g_runtime_cvl = g_cvl_voltage;
      g_runtime_proto_ccl_cap = proto_ccl_cap;
      g_runtime_proto_dcl_cap = proto_dcl_cap;
    } else {
      g_runtime_cvl = g_cvl_voltage;
      g_runtime_proto_ccl_cap = 0.0f;
      g_runtime_proto_dcl_cap = 0.0f;
    }

    if(factor_charge == 0.0) { alarm_flags |= 0x08; sys_error_msg = "INFO: TEMP CHARGE STOP"; }
    if(factor_discharge == 0.0) { alarm_flags |= 0x08; sys_error_msg = "INFO: TEMP DISCHG STOP"; }
    g_runtime_proto_uv_hit = proto_under_v_hit;
    if(proto_under_v_hit) { alarm_flags |= 0x10; safe_dis = 0; if(sys_error_msg == "OK") sys_error_msg = "ALARM: PACK UNDERVOLT"; }
    if( (alarm_flags & 0x02) || (alarm_flags & 0x10) || (alarm_flags & 0x40) ) { safe_chg = 0; safe_dis = 0; } 
    if (!g_maint_charge_mode) {
      if (victronData.avgSOC >= 99) safe_chg = count * 2.0;
      if (victronData.avgSOC == 100) safe_chg = 0.0;
    } else {
      float maint_target = g_maint_target_v;
      if (maint_target < 40.0f) maint_target = 40.0f;
      if (maint_target > g_safe_volt) maint_target = g_safe_volt;
      if (victronData.avgVoltage >= maint_target && safe_chg > count * 2.0f) safe_chg = count * 2.0f;
    }

    // Optional auto-balance watchdog: if target was not reached for 30 days, keep a small charge allowance.
    g_runtime_auto_balance_due = false;
    if (g_auto_balance_enable) {
      time_t t_now;
      time(&t_now);
      if (t_now > 1700000000) {
        uint32_t now_ts = (uint32_t)t_now;
        float bal_target = g_maint_target_v;
        if (bal_target < 40.0f) bal_target = 40.0f;
        if (bal_target > g_safe_volt) bal_target = g_safe_volt;
        bool reached_now = victronData.avgVoltage >= bal_target;
        if (reached_now && !g_runtime_balance_reached_prev) {
          g_auto_balance_last_ts = now_ts;
          saveAutoBalanceTimestamp(g_auto_balance_last_ts);
        }
        g_runtime_balance_reached_prev = reached_now;

        if (!reached_now && !g_maint_charge_mode) {
          const uint32_t window_s = 30UL * 24UL * 3600UL;
          if (g_auto_balance_last_ts == 0 || (now_ts > g_auto_balance_last_ts && (now_ts - g_auto_balance_last_ts) > window_s)) {
            g_runtime_auto_balance_due = true;
            if (!(alarm_flags & 0x02) && !(alarm_flags & 0x10) && factor_charge > 0.0f) {
              float hold_chg = count * 2.0f;
              if (safe_chg < hold_chg) safe_chg = hold_chg;
              if (sys_error_msg == "OK") sys_error_msg = "INFO: AUTO BALANCE ACTIVE";
            }
          }
        }
      }
    }
    
    victronData.maxChargeCurrent = safe_chg; victronData.maxDischargeCurrent = safe_dis;
  } else { victronData.activePacks = 0; victronData.totalCurrent = 0; victronData.totalPower = 0; victronData.maxChargeCurrent = 0; victronData.maxDischargeCurrent = 0; g_runtime_proto_uv_hit = false; g_runtime_auto_balance_due = false; g_runtime_balance_reached_prev = false; alarm_flags |= 0x80; }
}

void sendCanFrame(uint32_t id, uint8_t *data) {
  bool ok = false;
#if defined(BOARD_M5STACK)
  ok = (CAN0.sendMsgBuf(id, 0, 8, data) == CAN_OK);
#elif HAS_TWAI
  twai_message_t m;
  m.identifier = id;
  m.extd = 0;
  m.rtr = 0;
  m.data_length_code = 8;
  memcpy(m.data, data, 8);
  ok = (twai_transmit(&m, pdMS_TO_TICKS(5)) == ESP_OK);
#endif
  if (ok) {
    can_error_flag = false;
#ifdef MODE_SMART_WIFI
    debug_can_status = "CAN OK";
#endif
  } else {
    can_error_flag = true;
#ifdef MODE_SMART_WIFI
    debug_can_status = "TX ERR";
#endif
  }
  stat_tx_count++;
}

void sendVictronCAN() {
  uint8_t d[8] = {0}; float cvl = (g_runtime_cvl > 0.1f) ? g_runtime_cvl : g_cvl_voltage;
  int cv = (int)(cvl * 10), ccl = (int)(victronData.maxChargeCurrent * 10), dcl = (int)(victronData.maxDischargeCurrent * 10);
  d[0] = cv & 0xFF; d[1] = cv >> 8; d[2] = ccl & 0xFF; d[3] = ccl >> 8; d[4] = dcl & 0xFF; d[5] = dcl >> 8;
  sendCanFrame(0x351, d); delay(2);
  d[0] = (int)victronData.avgSOC & 0xFF; d[1] = (int)victronData.avgSOC >> 8; d[2] = (int)victronData.avgSOH & 0xFF; d[3] = (int)victronData.avgSOH >> 8;
  int cap = (int)(victronData.totalCapacity * 10); d[4] = cap & 0xFF; d[5] = cap >> 8; d[6]=0; d[7]=0;
  sendCanFrame(0x355, d); delay(2);
  int v = (int)(victronData.avgVoltage * 100), i = (int)(victronData.totalCurrent * 10), t = (int)(victronData.avgTemp * 10);
  d[0] = v & 0xFF; d[1] = v >> 8; d[2] = i & 0xFF; d[3] = i >> 8; d[4] = t & 0xFF; d[5] = t >> 8; d[6]=0; d[7]=0;
  sendCanFrame(0x356, d); delay(2);
  memset(d, 0, 8); 
  if(alarm_flags & 0x01) d[4] |= 0x80; if(alarm_flags & 0x40) d[4] |= 0x80; if(alarm_flags & 0x02) d[4] |= 0x40; if(alarm_flags & 0x08) d[4] |= 0x20; if(alarm_flags & 0x10) d[4] |= 0x10; 
  if(g_can_protocol == 2 && victronData.maxChargeCurrent < 0.1) d[4] |= 0x02; if(g_can_protocol == 2 && victronData.maxDischargeCurrent < 0.1) d[4] |= 0x01; 
  sendCanFrame(0x35A, d); delay(2);
  if(g_can_protocol == 1) { char n[] = "PYLON   "; sendCanFrame(0x35E, (uint8_t*)n); }
  else if(g_can_protocol == 2) { char n[] = "SMA     "; sendCanFrame(0x35E, (uint8_t*)n); }
  else { char n[] = "TOPBAND "; sendCanFrame(0x35E, (uint8_t*)n); } 
}

#ifdef MODE_SMART_WIFI
void loadHistory() {
  histStore.begin("h", true);
  for (int i = 0; i < 7; i++) { daily_in[i] = histStore.getFloat(("i" + String(i)).c_str(), 0); daily_out[i] = histStore.getFloat(("o" + String(i)).c_str(), 0); }
  size_t storedLen = histStore.getBytesLength("hdat");
  if(histStore.isKey("hidx") && storedLen == sizeof(powerHistory)) { historyIdx = histStore.getInt("hidx", 0); histStore.getBytes("hdat", powerHistory, sizeof(powerHistory)); } 
  else { for(int i=0; i<HISTORY_LEN; i++) powerHistory[i] = 0; }
  histStore.end();
}
void saveHistory(bool includeGraph) {
  histStore.begin("h", false);
  for (int i = 0; i < 7; i++) { histStore.putFloat(("i" + String(i)).c_str(), daily_in[i]); histStore.putFloat(("o" + String(i)).c_str(), daily_out[i]); }
  if(includeGraph) { histStore.putInt("hidx", historyIdx); histStore.putBytes("hdat", powerHistory, sizeof(powerHistory)); }
  histStore.end(); addToLog("Stats Saved", false);
}
void shiftHistory() { for (int i = 6; i > 0; i--) { daily_in[i] = daily_in[i - 1]; daily_out[i] = daily_out[i - 1]; } daily_in[0] = 0; daily_out[0] = 0; saveHistory(false); }
void calculateEnergy() {
  unsigned long now = millis(); if (last_energy_calc == 0) { last_energy_calc = now; return; }
  double h = (now - last_energy_calc) / 3600000.0; last_energy_calc = now;
  double kw = victronData.totalPower / 1000.0;
  if (kw > 0) daily_in[0] += (kw * h); else daily_out[0] += (fabs(kw) * h);
  struct tm t; if (getLocalTime(&t)) { if (current_day != -1 && current_day != t.tm_mday) shiftHistory(); current_day = t.tm_mday; if (t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec < 5) { if(now - last_flash_save > 60000) { saveHistory(false); last_flash_save = now; } } }
}
void updateHistory() { powerHistory[historyIdx] = (int16_t)victronData.totalPower; historyIdx++; if (historyIdx >= HISTORY_LEN) historyIdx = 0; }
void initSD() {
  if (!g_sd_enable || !HAS_SD_CARD) return;
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) { sd_ok = true; sd_error_flag = false; } else { sd_ok = false; sd_error_flag = true; addToLog("SD: No Card", true); }
}
void writeLogToSD() {
  if (!g_sd_enable || !sd_ok) return;
  File f = SD.open("/log.csv", FILE_APPEND);
  if (f) { f.printf("%s;%.1f;%.0f;%.1f\n", getTimeStr().c_str(), victronData.avgSOC, victronData.totalPower, victronData.totalCurrent); f.close(); } else { sd_error_flag = true; }
}
void handleSDDownload() { if(!server.hasArg("file")) { server.send(400, "text/plain", "Bad Request"); return; } String path = server.arg("file"); if (!sd_ok || !SD.exists(path)) { server.send(404, "text/plain", "File Not Found"); return; } File f = SD.open(path); if (f) { server.streamFile(f, "application/octet-stream"); f.close(); } }
void handleSpyDownload() { if (!sd_ok) { server.send(404, "text/plain", "No SD"); return; } File f = SD.open("/spy.txt"); if (f) { server.streamFile(f, "text/plain"); f.close(); } else server.send(404, "text/plain", "No Spy Log"); }
void handleSDList() { if(!sd_ok) { server.send(200, "application/json", "[]"); return; } String out = "["; File root = SD.open("/"); File file = root.openNextFile(); bool first = true; while(file){ if(!first) out += ","; out += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}"; first = false; file = root.openNextFile(); } out += "]"; server.send(200, "application/json", out); }
void handleSDDelete() { if(!sd_ok) { server.send(500, "text/plain", "No SD"); return; } if(server.hasArg("file")) { String f = server.arg("file"); if(SD.exists(f)) { SD.remove(f); server.send(200, "text/plain", "Deleted"); } else server.send(404, "text/plain", "Not Found"); } else if(server.hasArg("all")) { File root = SD.open("/"); File file = root.openNextFile(); while(file){ String n = file.name(); file = root.openNextFile(); SD.remove("/"+n); } server.send(200, "text/plain", "Wiped"); } else server.send(400, "text/plain", "Bad Args"); }
bool sendHaDiscovery() {
    if(!mqtt.connected()) return false;
    bool ok = true;
    String dev = "\"dev\":{\"ids\":\"vic_gw_1\",\"name\":\"Topband Gateway\",\"mdl\":\"V" + String(FW_VERSION) + "\",\"mf\":\"DIY\"}";
    String av = "";
    String p1 = "{\"name\":\"SOC\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.soc}}\",\"unit_of_meas\":\"%\",\"dev_cla\":\"battery\",\"uniq_id\":\"vic_soc\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/soc/config", p1.c_str(), true);
    String p2 = "{\"name\":\"Power\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.p}}\",\"unit_of_meas\":\"W\",\"dev_cla\":\"power\",\"uniq_id\":\"vic_p\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/p/config", p2.c_str(), true);
    String p3 = "{\"name\":\"Voltage\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.v}}\",\"unit_of_meas\":\"V\",\"dev_cla\":\"voltage\",\"uniq_id\":\"vic_v\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/v/config", p3.c_str(), true);
    String p4 = "{\"name\":\"Current\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.i}}\",\"unit_of_meas\":\"A\",\"dev_cla\":\"current\",\"uniq_id\":\"vic_i\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/i/config", p4.c_str(), true);

    String p5 = "{\"name\":\"Active BMS\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.active}}\",\"unit_of_meas\":\"packs\",\"icon\":\"mdi:battery-multiple\",\"uniq_id\":\"vic_active\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/active/config", p5.c_str(), true);
    String p6 = "{\"name\":\"Max Charge Current\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.max_chg}}\",\"unit_of_meas\":\"A\",\"dev_cla\":\"current\",\"uniq_id\":\"vic_max_chg\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/max_chg/config", p6.c_str(), true);
    String p7 = "{\"name\":\"Max Discharge Current\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.max_dis}}\",\"unit_of_meas\":\"A\",\"dev_cla\":\"current\",\"uniq_id\":\"vic_max_dis\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/max_dis/config", p7.c_str(), true);
    String p8 = "{\"name\":\"Alarm\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.alarm}}\",\"icon\":\"mdi:alert\",\"uniq_id\":\"vic_alarm\"," + dev + "}";
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/alarm/config", p8.c_str(), true);

    for (int i = 0; i < MAX_BMS; i++) {
      String idx = String(i);
      String t_soc = "homeassistant/sensor/vic_gw_1/bms" + idx + "_soc/config";
      String t_v = "homeassistant/sensor/vic_gw_1/bms" + idx + "_v/config";
      String t_i = "homeassistant/sensor/vic_gw_1/bms" + idx + "_i/config";
      String t_min = "homeassistant/sensor/vic_gw_1/bms" + idx + "_min_cell/config";
      String t_max = "homeassistant/sensor/vic_gw_1/bms" + idx + "_max_cell/config";
      String t_on = "homeassistant/binary_sensor/vic_gw_1/bms" + idx + "_online/config";

      if (g_mqtt_full && i < g_bms_count) {
        String s_soc = "{\"name\":\"BMS " + idx + " SOC\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.bms[" + idx + "].soc | default(none)}}\",\"unit_of_meas\":\"%\",\"dev_cla\":\"battery\",\"uniq_id\":\"vic_bms" + idx + "_soc\"," + dev + "}";
        String s_v = "{\"name\":\"BMS " + idx + " Voltage\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.bms[" + idx + "].pack_v | default(none)}}\",\"unit_of_meas\":\"V\",\"dev_cla\":\"voltage\",\"uniq_id\":\"vic_bms" + idx + "_v\"," + dev + "}";
        String s_i = "{\"name\":\"BMS " + idx + " Current\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.bms[" + idx + "].current | default(none)}}\",\"unit_of_meas\":\"A\",\"dev_cla\":\"current\",\"uniq_id\":\"vic_bms" + idx + "_i\"," + dev + "}";
        String s_min = "{\"name\":\"BMS " + idx + " Min Cell\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.bms[" + idx + "].min_cell | default(none)}}\",\"unit_of_meas\":\"V\",\"dev_cla\":\"voltage\",\"uniq_id\":\"vic_bms" + idx + "_min_cell\"," + dev + "}";
        String s_max = "{\"name\":\"BMS " + idx + " Max Cell\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{value_json.bms[" + idx + "].max_cell | default(none)}}\",\"unit_of_meas\":\"V\",\"dev_cla\":\"voltage\",\"uniq_id\":\"vic_bms" + idx + "_max_cell\"," + dev + "}";
        String s_on = "{\"name\":\"BMS " + idx + " Online\",\"stat_t\":\"victron/data\",\"val_tpl\":\"{{ 'ON' if (value_json.bms[" + idx + "].online | default(false)) else 'OFF' }}\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"dev_cla\":\"connectivity\",\"uniq_id\":\"vic_bms" + idx + "_online\"," + dev + "}";
        ok &= mqtt.publish(t_soc.c_str(), s_soc.c_str(), true);
        ok &= mqtt.publish(t_v.c_str(), s_v.c_str(), true);
        ok &= mqtt.publish(t_i.c_str(), s_i.c_str(), true);
        ok &= mqtt.publish(t_min.c_str(), s_min.c_str(), true);
        ok &= mqtt.publish(t_max.c_str(), s_max.c_str(), true);
        ok &= mqtt.publish(t_on.c_str(), s_on.c_str(), true);
      } else {
        ok &= mqtt.publish(t_soc.c_str(), "", true);
        ok &= mqtt.publish(t_v.c_str(), "", true);
        ok &= mqtt.publish(t_i.c_str(), "", true);
        ok &= mqtt.publish(t_min.c_str(), "", true);
        ok &= mqtt.publish(t_max.c_str(), "", true);
        ok &= mqtt.publish(t_on.c_str(), "", true);
      }
    }
    return ok;
}

bool clearHaDiscovery() {
    if(!mqtt.connected()) return false;
    bool ok = true;
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/soc/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/p/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/v/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/i/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/active/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/max_chg/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/max_dis/config", "", true);
    ok &= mqtt.publish("homeassistant/sensor/vic_gw_1/alarm/config", "", true);
    for (int i = 0; i < MAX_BMS; i++) {
      String idx = String(i);
      ok &= mqtt.publish(("homeassistant/sensor/vic_gw_1/bms" + idx + "_soc/config").c_str(), "", true);
      ok &= mqtt.publish(("homeassistant/sensor/vic_gw_1/bms" + idx + "_v/config").c_str(), "", true);
      ok &= mqtt.publish(("homeassistant/sensor/vic_gw_1/bms" + idx + "_i/config").c_str(), "", true);
      ok &= mqtt.publish(("homeassistant/sensor/vic_gw_1/bms" + idx + "_min_cell/config").c_str(), "", true);
      ok &= mqtt.publish(("homeassistant/sensor/vic_gw_1/bms" + idx + "_max_cell/config").c_str(), "", true);
      ok &= mqtt.publish(("homeassistant/binary_sensor/vic_gw_1/bms" + idx + "_online/config").c_str(), "", true);
    }
    return ok;
}

void handleHaDiscoverySend() {
  if (!g_mqtt_enable || g_mqtt_server == "") { server.send(400, "application/json", "{\"ok\":false,\"err\":\"mqtt disabled\"}"); return; }
  mqttReconnect();
  if (!mqtt.connected()) { server.send(500, "application/json", "{\"ok\":false,\"err\":\"mqtt not connected\"}"); return; }
  if (!sendHaDiscovery()) {
    addToLog("HA Discovery: publish failed", true);
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"mqtt publish failed\"}");
    return;
  }
  addToLog("HA Discovery: sent", false);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleHaDiscoveryClear() {
  if (!g_mqtt_enable || g_mqtt_server == "") { server.send(400, "application/json", "{\"ok\":false,\"err\":\"mqtt disabled\"}"); return; }
  mqttReconnect();
  if (!mqtt.connected()) { server.send(500, "application/json", "{\"ok\":false,\"err\":\"mqtt not connected\"}"); return; }
  if (!clearHaDiscovery()) {
    addToLog("HA Discovery: clear publish failed", true);
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"mqtt publish failed\"}");
    return;
  }
  addToLog("HA Discovery: cleared", false);
  server.send(200, "application/json", "{\"ok\":true}");
}

void mqttReconnect() {
  if (!g_mqtt_enable || g_mqtt_server == "") return;
  if (!mqtt.connected()) {
    if (mqtt.connect("VictronGW", g_mqtt_user.c_str(), g_mqtt_pass.c_str())) {
      mqtt.publish("victron/status", "online", true);
      if (false && g_ha_enable) {
        if (!sendHaDiscovery()) addToLog("HA Discovery: publish failed", true);
      }
    }
  }
}


void sendMqttData() {
  if (g_mqtt_enable && mqtt.connected()) {
    mqtt.publish("victron/status", "online", true);
    String json = "{"; json += "\"soc\":" + String(victronData.avgSOC, 1) + ","; json += "\"p\":" + String(victronData.totalPower, 0) + ",";
    json += "\"i\":" + String(victronData.totalCurrent, 1) + ","; json += "\"v\":" + String(victronData.avgVoltage, 2) + ",";
    json += "\"alarm\":\"" + sys_error_msg + "\","; json += "\"active\":" + String(victronData.activePacks);
    // Core live values
    json += ",\"max_chg\":" + String(victronData.maxChargeCurrent, 1) + ",\"max_dis\":" + String(victronData.maxDischargeCurrent, 1);
    if (g_mqtt_full) {
      json += ",\"bms\":[";
      for (int i = 0; i < g_bms_count; i++) {
        if (i > 0) json += ",";
        bool on = bms[i].valid && (millis() - bms[i].last_seen < 60000);
        json += "{\"id\":" + String(i) + ",\"online\":" + String(on ? "true" : "false");
        if (on) {
          json += ",\"soc\":" + String(bms[i].soc);
          json += ",\"current\":" + String(bms[i].current, 1);
          json += ",\"pack_v\":" + String(bms[i].voltage, 2);
          json += ",\"soh\":" + String(bms[i].soh);
          json += ",\"min_cell\":" + String(bms[i].minCellV, 3);
          json += ",\"max_cell\":" + String(bms[i].maxCellV, 3);
          json += ",\"cells\":[";
          for (int c = 0; c < bms[i].cell_count; c++) {
            if (c > 0) json += ",";
            json += String(bms[i].cells[c], 3);
          }
          json += "]";
        }
        json += "}";
      }
      json += "]";
    }
    json += "}"; mqtt.publish("victron/data", json.c_str());
  }
}

bool svcParseBmsId(int &id) {
  if (!server.hasArg("id")) return false;
  id = server.arg("id").toInt();
  if (id < 0 || id >= g_bms_count || id >= MAX_BMS) return false;
  return true;
}

float svcArgFloat(const char *name, float def) {
  if (!server.hasArg(name)) return def;
  return server.arg(name).toFloat();
}

void handleServiceBmsTimeGet() {
  int id;
  if (!svcParseBmsId(id)) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad id\"}"); return; }

  TBFrame fr;
  String err;
  if (!tbRequestFrame((uint8_t)id, 0x4D, nullptr, 0, fr, err, true)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"" + jsonEscape(err) + "\"}");
    return;
  }
  if (fr.cid2 != 0x00) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"BMS code 0x" + String(fr.cid2, HEX) + "\"}");
    return;
  }
  TBDate d;
  if (!tbParseDateFromFrame(fr, d)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"date parse failed\"}");
    return;
  }
  String j = "{";
  j += "\"ok\":true,";
  j += "\"id\":" + String(id) + ",";
  j += "\"date\":{\"year\":" + String(d.year) + ",\"month\":" + String(d.month) + ",\"day\":" + String(d.day) + ",\"hour\":" + String(d.hour) + ",\"minute\":" + String(d.minute) + ",\"second\":" + String(d.second) + "}";
  j += "}";
  server.send(200, "application/json", j);
}

void handleServiceBmsTimeSet() {
  int id;
  if (!svcParseBmsId(id)) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad id\"}"); return; }

  TBDate d = {};
  if (server.hasArg("year")) {
    d.year = server.arg("year").toInt();
    d.month = server.arg("month").toInt();
    d.day = server.arg("day").toInt();
    d.hour = server.arg("hour").toInt();
    d.minute = server.arg("minute").toInt();
    d.second = server.arg("second").toInt();
  } else {
    struct tm t;
    if (!getLocalTime(&t)) {
      server.send(500, "application/json", "{\"ok\":false,\"err\":\"gateway time unavailable\"}");
      return;
    }
    d.year = t.tm_year + 1900;
    d.month = t.tm_mon + 1;
    d.day = t.tm_mday;
    d.hour = t.tm_hour;
    d.minute = t.tm_min;
    d.second = t.tm_sec;
  }

  uint8_t info7[7] = {
    (uint8_t)((d.year >> 8) & 0xFF), (uint8_t)(d.year & 0xFF),
    (uint8_t)d.month, (uint8_t)d.day, (uint8_t)d.hour, (uint8_t)d.minute, (uint8_t)d.second
  };

  TBFrame fr;
  String err;
  bool ok = tbRequestFrame((uint8_t)id, 0x4E, info7, 7, fr, err, true);
  if ((!ok || fr.cid2 != 0x00)) {
    // Fallback for variants requiring BMS id in payload.
    uint8_t info8[8] = {
      (uint8_t)id,
      (uint8_t)((d.year >> 8) & 0xFF), (uint8_t)(d.year & 0xFF),
      (uint8_t)d.month, (uint8_t)d.day, (uint8_t)d.hour, (uint8_t)d.minute, (uint8_t)d.second
    };
    ok = tbRequestFrame((uint8_t)id, 0x4E, info8, 8, fr, err, true);
  }

  if (!ok) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"" + jsonEscape(err) + "\"}");
    return;
  }
  if (fr.cid2 != 0x00) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"BMS code 0x" + String(fr.cid2, HEX) + "\"}");
    return;
  }

  String j = "{";
  j += "\"ok\":true,";
  j += "\"id\":" + String(id) + ",";
  j += "\"set\":{\"year\":" + String(d.year) + ",\"month\":" + String(d.month) + ",\"day\":" + String(d.day) + ",\"hour\":" + String(d.hour) + ",\"minute\":" + String(d.minute) + ",\"second\":" + String(d.second) + "}";
  j += "}";
  server.send(200, "application/json", j);
}

void handleServiceAutoCfgApply() {
  int id;
  if (!svcParseBmsId(id)) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad id\"}"); return; }
  float buf_cell_mv = svcArgFloat("buf_cell_mv", 20.0f);
  float buf_pack_v = svcArgFloat("buf_pack_v", 0.30f);
  float buf_temp_c = svcArgFloat("buf_temp_c", 2.0f);
  float buf_curr_pct = svcArgFloat("buf_curr_pct", 10.0f);

  TBFrame fr;
  String err;
  if (!tbRequestFrame((uint8_t)id, 0x47, nullptr, 0, fr, err, true) || fr.cid2 != 0x00) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"system parameter read failed\"}");
    return;
  }

  TBSysParam sp;
  if (!tbParseSystemParameterFromFrame(fr, sp)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"system parameter parse failed\"}");
    return;
  }
  if (!tbSystemParamPlausible(sp)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"system parameter out of plausible range\"}");
    return;
  }

  float safe_cell, safe_volt, tc_min, tc_max, td_min, td_max, chg_a, dis_a, cvl_v;
  tbBuildAutoCfgSuggestion(sp, buf_cell_mv, buf_pack_v, buf_temp_c, buf_curr_pct,
                           safe_cell, safe_volt, tc_min, tc_max, td_min, td_max, chg_a, dis_a, cvl_v);

  g_expert_mode = true;
  g_safe_cell = safe_cell;
  g_safe_volt = safe_volt;
  g_tc_min = tc_min;
  g_tc_max = tc_max;
  g_td_min = td_min;
  g_td_max = td_max;
  g_charge_amps = chg_a;
  g_discharge_amps = dis_a;
  g_cvl_voltage = cvl_v;

  String j = "{";
  j += "\"ok\":true,";
  j += "\"id\":" + String(id) + ",";
  j += "\"applied\":{";
  j += "\"s_cel\":" + String(g_safe_cell, 3) + ",";
  j += "\"s_vol\":" + String(g_safe_volt, 2) + ",";
  j += "\"t_c_min\":" + String(g_tc_min, 1) + ",";
  j += "\"t_c_max\":" + String(g_tc_max, 1) + ",";
  j += "\"t_d_min\":" + String(g_td_min, 1) + ",";
  j += "\"t_d_max\":" + String(g_td_max, 1) + ",";
  j += "\"chg\":" + String(g_charge_amps, 1) + ",";
  j += "\"dis\":" + String(g_discharge_amps, 1) + ",";
  j += "\"cvl\":" + String(g_cvl_voltage, 2);
  j += "}}";
  server.send(200, "application/json", j);
}

void handleServiceBmsDiag() {
  int id;
  if (!svcParseBmsId(id)) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad id\"}"); return; }

  float buf_cell_mv = svcArgFloat("buf_cell_mv", 20.0f);
  float buf_pack_v = svcArgFloat("buf_pack_v", 0.30f);
  float buf_temp_c = svcArgFloat("buf_temp_c", 2.0f);
  float buf_curr_pct = svcArgFloat("buf_curr_pct", 10.0f);

  TBFrame fr_manu, fr_date, fr_alarm, fr_sys, fr_hist, fr_aval;
  String err_manu, err_date, err_alarm, err_sys, err_hist, err_aval;

  bool manu_ok = tbRequestFrame((uint8_t)id, 0x51, nullptr, 0, fr_manu, err_manu, true) && fr_manu.cid2 == 0x00;
  bool date_ok = tbRequestFrame((uint8_t)id, 0x4D, nullptr, 0, fr_date, err_date, true) && fr_date.cid2 == 0x00;
  uint8_t alarm_req[1] = {(uint8_t)id};
  bool alarm_ok = tbRequestFrame((uint8_t)id, 0x44, alarm_req, 1, fr_alarm, err_alarm, true) && fr_alarm.cid2 == 0x00;
  bool sys_ok = tbRequestFrame((uint8_t)id, 0x47, nullptr, 0, fr_sys, err_sys, true) && fr_sys.cid2 == 0x00;
  uint8_t hist_req[2] = {0x00, (uint8_t)id};  // first event
  bool hist_ok = tbRequestFrame((uint8_t)id, 0x4B, hist_req, 2, fr_hist, err_hist, true) && fr_hist.cid2 == 0x00;
  uint8_t aval_req[1] = {(uint8_t)id};
  bool aval_ok = tbRequestFrame((uint8_t)id, 0x42, aval_req, 1, fr_aval, err_aval, true) && fr_aval.cid2 == 0x00;

  String hw = "", sw = "", mid = "";
  TBDate bms_date = {};
  TBAlarmInfoParsed ai = {};
  TBSysParam sp = {};
  TBDate hist_date = {};
  int hist_event = -1;
  uint64_t hist_status = 0;
  float hist_i = 0, hist_v = 0, hist_rem = 0;
  TBAnalogDiag ad = {};

  if (manu_ok) manu_ok = tbParseManufacturerFromFrame(fr_manu, hw, sw, mid);
  if (date_ok) date_ok = tbParseDateFromFrame(fr_date, bms_date);
  if (alarm_ok) {
    alarm_ok = tbParseAlarmInfoFromFrame(fr_alarm, ai);
    if (alarm_ok) {
      g_bms_alarm_bits[id] = ai.status_bits;
      g_bms_alarm_status_count[id] = ai.status_count;
      g_bms_alarm_last_seen[id] = millis();
    }
  }
  if (sys_ok) {
    sys_ok = tbParseSystemParameterFromFrame(fr_sys, sp);
    if (sys_ok && !tbSystemParamPlausible(sp)) { sys_ok = false; err_sys = "system parameter out of plausible range"; }
  }
  if (hist_ok) hist_ok = tbParseHistoricalFirstFromFrame(fr_hist, hist_date, hist_event, hist_status, hist_i, hist_v, hist_rem);
  if (aval_ok) aval_ok = tbParseAnalogDiagFromFrame(fr_aval, ad);

  float sug_cell = 0, sug_volt = 0, sug_tcmin = 0, sug_tcmax = 0, sug_tdmin = 0, sug_tdmax = 0, sug_chg = 0, sug_dis = 0, sug_cvl = 0;
  if (sys_ok) {
    tbBuildAutoCfgSuggestion(sp, buf_cell_mv, buf_pack_v, buf_temp_c, buf_curr_pct,
                             sug_cell, sug_volt, sug_tcmin, sug_tcmax, sug_tdmin, sug_tdmax, sug_chg, sug_dis, sug_cvl);
  }

  String j = "{";
  j += "\"ok\":true,";
  j += "\"id\":" + String(id) + ",";

  j += "\"manufacturer\":{\"ok\":" + String(manu_ok ? "true" : "false");
  if (manu_ok) {
    j += ",\"hw\":\"" + jsonEscape(hw) + "\",\"sw\":\"" + jsonEscape(sw) + "\",\"id\":\"" + jsonEscape(mid) + "\"";
  } else {
    j += ",\"err\":\"" + jsonEscape(err_manu) + "\"";
  }
  j += "},";

  j += "\"date\":{\"ok\":" + String(date_ok ? "true" : "false");
  if (date_ok) {
    j += ",\"year\":" + String(bms_date.year) + ",\"month\":" + String(bms_date.month) + ",\"day\":" + String(bms_date.day) + ",\"hour\":" + String(bms_date.hour) + ",\"minute\":" + String(bms_date.minute) + ",\"second\":" + String(bms_date.second);
  } else {
    j += ",\"err\":\"" + jsonEscape(err_date) + "\"";
  }
  j += "},";

  j += "\"analog\":{\"ok\":" + String(aval_ok ? "true" : "false");
  if (aval_ok) {
    j += ",\"id\":" + String(ad.id);
    j += ",\"cells\":" + String(ad.cell_count);
    j += ",\"temps\":" + String(ad.temp_count);
    j += ",\"pack_v\":" + String(ad.pack_v, 2);
    j += ",\"current\":" + String(ad.current_a, 2);
    j += ",\"soc\":" + String(ad.soc);
    j += ",\"soh\":" + String(ad.soh);
    j += ",\"min_cell\":" + String(ad.min_cell_v, 3);
    j += ",\"max_cell\":" + String(ad.max_cell_v, 3);
    j += ",\"rem_ah\":" + String(ad.rem_ah, 2);
    j += ",\"full_ah\":" + String(ad.full_ah, 2);
  } else {
    j += ",\"err\":\"" + jsonEscape(err_aval) + "\"";
  }
  j += "},";

  j += "\"alarm\":{\"ok\":" + String(alarm_ok ? "true" : "false");
  if (alarm_ok) {
    j += ",\"status_count\":" + String(ai.status_count);
    j += ",\"status_hex\":\"" + u64Hex(ai.status_bits) + "\"";
    j += ",\"critical\":" + String(tbHasCriticalAlarm(ai.status_bits) ? "true" : "false");
    j += ",\"active\":[";
    bool first = true;
    for (uint8_t b = 0; b < 64; b++) {
      if (ai.status_bits & (1ULL << b)) {
        if (!first) j += ",";
        j += "{\"bit\":" + String(b) + ",\"name\":\"" + jsonEscape(String(tbAlarmBitName(b))) + "\"}";
        first = false;
      }
    }
    j += "]";
  } else {
    j += ",\"err\":\"" + jsonEscape(err_alarm) + "\"";
  }
  j += "},";

  j += "\"system\":{\"ok\":" + String(sys_ok ? "true" : "false");
  if (sys_ok) {
    j += ",\"cell_high_v\":" + String(sp.cell_high_v, 3);
    j += ",\"cell_low_v\":" + String(sp.cell_low_v, 3);
    j += ",\"cell_under_v\":" + String(sp.cell_under_v, 3);
    j += ",\"module_high_v\":" + String(sp.module_high_v, 2);
    j += ",\"module_low_v\":" + String(sp.module_low_v, 2);
    j += ",\"module_under_v\":" + String(sp.module_under_v, 2);
    j += ",\"charge_high_t\":" + String(sp.charge_high_t, 1);
    j += ",\"charge_low_t\":" + String(sp.charge_low_t, 1);
    j += ",\"discharge_high_t\":" + String(sp.discharge_high_t, 1);
    j += ",\"discharge_low_t\":" + String(sp.discharge_low_t, 1);
    j += ",\"charge_current_max_a\":" + String(sp.charge_current_max_a, 1);
    j += ",\"discharge_current_max_a\":" + String(sp.discharge_current_max_a, 1);
  } else {
    j += ",\"err\":\"" + jsonEscape(err_sys) + "\"";
  }
  j += "},";

  j += "\"historical\":{\"ok\":" + String(hist_ok ? "true" : "false");
  if (hist_ok) {
    j += ",\"year\":" + String(hist_date.year) + ",\"month\":" + String(hist_date.month) + ",\"day\":" + String(hist_date.day) + ",\"hour\":" + String(hist_date.hour) + ",\"minute\":" + String(hist_date.minute) + ",\"second\":" + String(hist_date.second);
    j += ",\"event\":" + String(hist_event);
    j += ",\"status_hex\":\"" + u64Hex(hist_status) + "\"";
    j += ",\"pack_v\":" + String(hist_v, 2);
    j += ",\"current\":" + String(hist_i, 2);
    j += ",\"remain_ah\":" + String(hist_rem, 2);
  } else {
    j += ",\"err\":\"" + jsonEscape(err_hist) + "\"";
  }
  j += "},";

  j += "\"autocfg\":{\"ok\":" + String(sys_ok ? "true" : "false");
  if (sys_ok) {
    j += ",\"buffer\":{\"cell_mv\":" + String(buf_cell_mv, 1) + ",\"pack_v\":" + String(buf_pack_v, 2) + ",\"temp_c\":" + String(buf_temp_c, 1) + ",\"curr_pct\":" + String(buf_curr_pct, 1) + "}";
    j += ",\"suggest\":{\"s_cel\":" + String(sug_cell, 3) + ",\"s_vol\":" + String(sug_volt, 2) + ",\"t_c_min\":" + String(sug_tcmin, 1) + ",\"t_c_max\":" + String(sug_tcmax, 1) + ",\"t_d_min\":" + String(sug_tdmin, 1) + ",\"t_d_max\":" + String(sug_tdmax, 1) + ",\"chg\":" + String(sug_chg, 1) + ",\"dis\":" + String(sug_dis, 1) + ",\"cvl\":" + String(sug_cvl, 2) + "}";
  }
  j += "}";

  j += "}";
  server.send(200, "application/json", j);
}

void handleSim() { if (server.hasArg("act")) simulation_active = (server.arg("act") == "1"); server.send(200); }
void handleSave() {
  saveHistory(true); delay(100); preferences.begin("gateway", false); 
  if (server.hasArg("cnt")) g_bms_count = server.arg("cnt").toInt();
  if (server.hasArg("cells")) g_force_cell_count = server.arg("cells").toInt();
  if (server.hasArg("soc_mode")) g_soc_source_mode = server.arg("soc_mode").toInt();
  if (g_soc_source_mode < 0 || g_soc_source_mode > 2) g_soc_source_mode = 2;
  g_maint_charge_mode = server.hasArg("maint_en");
  if (server.hasArg("maint_v")) g_maint_target_v = server.arg("maint_v").toFloat();
  if (g_maint_target_v < 40.0f) g_maint_target_v = 40.0f;
  if (g_maint_target_v > 65.0f) g_maint_target_v = 65.0f;
  g_auto_balance_enable = server.hasArg("ab_en");
  g_easy_mode = server.hasArg("easy");
  g_expert_mode = server.hasArg("exp");
  if (g_easy_mode) g_expert_mode = false;
  g_setup_done = true;
  if(g_expert_mode) {
      if(server.hasArg("chg")) g_charge_amps = server.arg("chg").toFloat();
      if(server.hasArg("dis")) g_discharge_amps = server.arg("dis").toFloat();
      if(server.hasArg("cvl")) g_cvl_voltage = server.arg("cvl").toFloat();
      if(server.hasArg("s_vol")) g_safe_volt = server.arg("s_vol").toFloat();
      if(server.hasArg("s_cel")) g_safe_cell = server.arg("s_cel").toFloat();
      if(server.hasArg("t_c_min")) g_tc_min = server.arg("t_c_min").toFloat();
      if(server.hasArg("t_c_max")) g_tc_max = server.arg("t_c_max").toFloat();
      if(server.hasArg("t_d_min")) g_td_min = server.arg("t_d_min").toFloat();
      if(server.hasArg("t_d_max")) g_td_max = server.arg("t_d_max").toFloat();
      if(server.hasArg("t_mode")) g_temp_mode = server.arg("t_mode").toInt();
      if(server.hasArg("s_drift")) g_safe_drift = server.arg("s_drift").toFloat();
  }  // Datasheet defaults outside expert mode.
  if (!g_expert_mode) {
    g_easy_mode = false;
    g_charge_amps = 30.0f;
    g_discharge_amps = 30.0f;
    g_cvl_voltage = 52.5f;
    g_safe_volt = 53.25f;
    g_safe_cell = 3.55f;
    g_safe_drift = 0.20f;
    g_tc_min = 5.0f;
    g_tc_max = 50.0f;
    g_td_min = -20.0f;
    g_td_max = 60.0f;
    g_maint_target_v = 51.0f;
  }
  g_sd_enable = server.hasArg("sd_en"); g_sd_spy = server.hasArg("spy"); g_serial_debug = server.hasArg("debug");
  preferences.putBool("sd_en", g_sd_enable); preferences.putBool("debug", g_serial_debug); preferences.putBool("spy", g_sd_spy);
  if(server.hasArg("theme")) g_theme_id = server.arg("theme").toInt();
  if(server.hasArg("can_proto")) g_can_protocol = server.arg("can_proto").toInt(); 
  g_mqtt_enable = server.hasArg("mq_en"); g_victron_enable = server.hasArg("vic_en");
  g_mqtt_full = server.hasArg("mq_full"); g_ha_enable = server.hasArg("ha_en"); 
  g_mqtt_server = server.arg("mq_ip"); g_mqtt_port = server.arg("mq_pt").toInt();
  g_mqtt_user = server.arg("mq_us"); g_mqtt_pass = server.arg("mq_pw");
  g_ntp_server = server.arg("ntp_svr"); g_timezone_offset = server.arg("tz_off").toInt();
  preferences.putInt("cnt", g_bms_count); preferences.putInt("cells", g_force_cell_count);
  preferences.putFloat("chg", g_charge_amps); preferences.putFloat("dis", g_discharge_amps);
  preferences.putBool("exp", g_expert_mode); preferences.putFloat("cvl", g_cvl_voltage);
  preferences.putInt("soc_mode", g_soc_source_mode);
  preferences.putBool("maint_en", g_maint_charge_mode);
  preferences.putFloat("maint_v", g_maint_target_v);
  preferences.putBool("ab_en", g_auto_balance_enable);
  preferences.putBool("easy", g_easy_mode); preferences.putBool("setupd", g_setup_done);
  preferences.putFloat("s_vol", g_safe_volt); preferences.putFloat("s_cel", g_safe_cell);
  preferences.putFloat("t_c_min", g_tc_min); preferences.putFloat("t_c_max", g_tc_max);
  preferences.putFloat("t_d_min", g_td_min); preferences.putFloat("t_d_max", g_td_max);
  preferences.putInt("t_mode", g_temp_mode); preferences.putFloat("s_drift", g_safe_drift);
  preferences.putBool("mq_en", g_mqtt_enable); preferences.putBool("mq_full", g_mqtt_full); preferences.putBool("vic_en", g_victron_enable); 
  preferences.putBool("ha_en", g_ha_enable); preferences.putInt("theme", g_theme_id); preferences.putInt("can_proto", g_can_protocol);
  preferences.putString("mq_ip", g_mqtt_server); preferences.putInt("mq_pt", g_mqtt_port);
  preferences.putString("mq_us", g_mqtt_user); preferences.putString("mq_pw", g_mqtt_pass);
  preferences.putString("ntp", g_ntp_server); preferences.putInt("tz", g_timezone_offset);
  preferences.end(); delay(100); server.sendHeader("Location", "/"); server.send(303); delay(500); ESP.restart();
}
void handleData() {
  String j = "{ \"victron\": {";
  j += "\"active\": " + String(victronData.activePacks) + ","; j += "\"total_amps\": " + String(victronData.totalCurrent, 1) + ",";
  j += "\"total_power\": " + String(victronData.totalPower, 0) + ","; j += "\"avg_soc\": " + String(victronData.avgSOC, 1) + ",";
  j += "\"avg_soh\": " + String(victronData.avgSOH, 1) + ","; j += "\"voltage\": " + String(victronData.avgVoltage, 2) + ",";
  j += "\"rem_cap\": " + String(victronData.remainCapacity, 0) + ","; j += "\"total_cap\": " + String(victronData.totalCapacity, 0) + ",";
  j += "\"avg_temp\": " + String(victronData.avgTemp, 1) + ","; 
  j += "\"max_chg\": " + String(victronData.maxChargeCurrent, 1) + ",";
  j += "\"max_dis\": " + String(victronData.maxDischargeCurrent, 1) + ",";
  j += "\"dyn_cvl\": " + String(g_runtime_cvl > 0.1f ? g_runtime_cvl : g_cvl_voltage, 2) + ",";
  j += "\"proto_ccl\": " + String(g_runtime_proto_ccl_cap, 1) + ",";
  j += "\"proto_dcl\": " + String(g_runtime_proto_dcl_cap, 1) + ",";
  j += "\"proto_uv_hit\": " + String(g_runtime_proto_uv_hit ? "true" : "false") + ",";
  j += "\"maint_en\": " + String(g_maint_charge_mode ? "true" : "false") + ",";
  j += "\"maint_v\": " + String(g_maint_target_v, 2) + ",";
  j += "\"autobal_due\": " + String(g_runtime_auto_balance_due ? "true" : "false") + ",";
  float conf_chg = g_bms_count * g_charge_amps;
  float conf_dis = g_bms_count * g_discharge_amps;
  j += "\"cfg_chg\": " + String(conf_chg, 1) + ",";
  j += "\"cfg_dis\": " + String(conf_dis, 1);
  j += "}, ";
  
  j += "\"config\": {"; j += "\"cnt\": " + String(g_bms_count) + ", "; j += "\"cells\": " + String(g_force_cell_count) + ", ";
  j += "\"chg\": " + String(g_charge_amps) + ", "; j += "\"dis\": " + String(g_discharge_amps) + ", ";
  j += "\"cvl\": " + String(g_cvl_voltage) + ", "; j += "\"exp\": " + String(g_expert_mode ? "true" : "false") + ", ";
  j += "\"s_vol\": " + String(g_safe_volt) + ", "; j += "\"s_cel\": " + String(g_safe_cell) + ", ";
  j += "\"t_c_min\": " + String(g_tc_min) + ", "; j += "\"t_c_max\": " + String(g_tc_max) + ", ";
  j += "\"t_d_min\": " + String(g_td_min) + ", "; j += "\"t_d_max\": " + String(g_td_max) + ", ";
  j += "\"t_mode\": " + String(g_temp_mode) + ", "; j += "\"s_drift\": " + String(g_safe_drift) + ", ";
  j += "\"debug\": " + String(g_serial_debug ? "true" : "false") + ", "; j += "\"spy\": " + String(g_sd_spy ? "true" : "false") + ", "; 
  j += "\"can_proto\": " + String(g_can_protocol) + ", "; j += "\"theme\": " + String(g_theme_id) + ", ";
  j += "\"ntp\": \"" + g_ntp_server + "\", "; j += "\"tz\": " + String(g_timezone_offset) + ", ";
  j += "\"sd\": " + String(g_sd_enable ? "true" : "false") + ", "; j += "\"vic_en\": " + String(g_victron_enable ? "true" : "false") + ", ";
  j += "\"mq_en\": " + String(g_mqtt_enable ? "true" : "false") + ", "; j += "\"mq_full\": " + String(g_mqtt_full ? "true" : "false") + ", ";
  j += "\"ha_en\": " + String(g_ha_enable ? "true" : "false") + ", "; j += "\"mq_ip\": \"" + g_mqtt_server + "\", ";
  j += "\"mq_pt\": " + String(g_mqtt_port) + ", "; j += "\"mq_us\": \"" + g_mqtt_user + "\", ";
  j += "\"fw\": \"" + String(FW_VERSION) + "\", ";
  j += "\"soc_mode\": " + String(g_soc_source_mode) + ", ";
  j += "\"maint_en\": " + String(g_maint_charge_mode ? "true" : "false") + ", ";
  j += "\"maint_v\": " + String(g_maint_target_v, 2) + ", ";
  j += "\"ab_en\": " + String(g_auto_balance_enable ? "true" : "false") + ", ";
  j += "\"ab_last\": " + String(g_auto_balance_last_ts) + ", ";
  j += "\"easy\": " + String(g_easy_mode ? "true" : "false") + ", "; j += "\"setup_done\": " + String(g_setup_done ? "true" : "false") + ", ";
  j += "\"dummy\":0 }, ";
  struct tm t; unsigned long ts = 0; if(getLocalTime(&t)) { time_t t_now; time(&t_now); ts = (unsigned long)t_now; }
  j += "\"ts\": " + String(ts) + ", "; String l = debug_log; l.replace("\"", "'"); l.replace("\n", ""); j += "\"log\": \"" + l + "\", "; 
  if(!g_victron_enable) j += "\"can_status\": \"DISABLED\", \"can_error\": false, ";
  else j += "\"can_status\": \"" + debug_can_status + "\", \"can_error\": " + String(can_error_flag ? "true" : "false") + ", ";
  if(!HAS_SD_CARD) { j += "\"sd_ok\": false, \"sd_err\": false, "; } else { j += "\"sd_ok\": " + String(sd_ok ? "true" : "false") + ", \"sd_err\": " + String(sd_error_flag ? "true" : "false") + ", "; }
  j += "\"safe_lock\": " + String(((alarm_flags & 0x02) || (alarm_flags & 0x10) || (alarm_flags & 0x40) || (alarm_flags & 0x80)) ? "true" : "false") + ", ";
  j += "\"history\": ["; int startIdx = historyIdx; 
  for (int i = 0; i < HISTORY_LEN; i++) { int idx = (startIdx + i) % HISTORY_LEN; if (i > 0) j += ","; j += String(powerHistory[idx]); }
  j += "], "; j += "\"days_in\": ["; for(int i=0; i<7; i++) { if(i>0) j+=","; j+=String(daily_in[i],2); } j += "], ";
  j += "\"days_out\": ["; for(int i=0; i<7; i++) { if(i>0) j+=","; j+=String(daily_out[i],2); } j += "], ";
  j += "\"energy\": {\"in\": " + String(daily_in[0], 2) + ", \"out\": " + String(daily_out[0], 2) + "}, ";
  j += "\"bms\": [";
  for (int i = 0; i < g_bms_count; i++) {
    if (i > 0) j += ",";
    bool on = bms[i].valid && (millis() - bms[i].last_seen < 60000) || simulation_active;
    j += "{ \"id\":" + String(i) + ", \"online\":" + (on ? "true" : "false");
    if (on) {
      j += ", \"soc\":" + String(bms[i].soc) + ", \"current\":" + String(bms[i].current, 1);
      j += ", \"pack_v\":" + String(bms[i].voltage, 2) + ", \"soh\":" + String(bms[i].soh);
      j += ", \"min_cell\":" + String(bms[i].minCellV, 3) + ", \"max_cell\":" + String(bms[i].maxCellV, 3);
      j += ", \"avg_temp\":" + String(bms[i].avgTemp, 1);
      j += ", \"temps\":[";
      for (int t = 0; t < bms[i].temp_count; t++) { if (t > 0) j += ","; String lbl = "T" + String(t + 1); if (t == 4) lbl = "MOS"; if (t == 5) lbl = "ENV"; if (t == 6) lbl = "BAL"; j += "{\"val\":" + String(bms[i].temps[t], 1) + ",\"lbl\":\"" + lbl + "\"}"; }
      j += "], \"cells\":["; for (int c = 0; c < bms[i].cell_count; c++) { if (c > 0) j += ","; j += String(bms[i].cells[c], 3); } j += "]";
    }
    j += "}";
  }
  j += "] }"; server.send(200, "application/json", j);
}
void handleRoot() {
  String cv;
  if(g_theme_id == 1) cv=":root{--bg-grad:#111;--card-bg:#222;--border:1px solid #444;--text:#eee;--text-sec:#aaa;--head-col:#1e3a8a;--head-txt:#fff;--bor-top:1px solid #444;--bor-right:1px solid #444;--input-bg:#333;--input-txt:#fff;}";
  else if(g_theme_id == 2) cv=":root{--bg-grad:#ddd;--card-bg:#fff;--border:2px solid #000;--text:#000;--text-sec:#333;--head-col:#333;--head-txt:#000;--bor-top:2px solid #000;--bor-right:2px solid #000;--input-bg:#fff;--input-txt:#000;}";
  else if(g_theme_id == 3) cv=":root{--bg-grad:#111;--card-bg:rgba(0,0,0,0.6);--border:1px solid rgba(255,255,255,0.1);--text:#fff;--text-sec:#ccc;--head-col:transparent;--head-txt:#fff;--bor-top:none;--bor-right:none;--input-bg:rgba(0,0,0,0.5);--input-txt:#fff;}";
  else if(g_theme_id == 4) cv=":root{--bg-grad:#050505;--card-bg:#0a0a0a;--border:1px solid #0ff;--text:#0ff;--text-sec:#088;--head-col:transparent;--head-txt:#0ff;--bor-top:1px solid #0ff;--bor-right:1px solid #0ff;--input-bg:#000;--input-txt:#0ff;}";
  else if(g_theme_id == 5) cv=":root{--bg-grad:#2b2b2b;--card-bg:#2b2b2b;--border:none;--text:#ddd;--text-sec:#888;--head-col:transparent;--head-txt:#eee;--bor-top:none;--bor-right:none;--input-bg:#252525;--input-txt:#eee;}";
  else if(g_theme_id == 6) cv=":root{--bg-grad:#222;--card-bg:#333;--border:1px solid #555;--text:#fff;--text-sec:#aaa;--head-col:#444;--head-txt:#fff;--bor-top:1px solid #555;--bor-right:1px solid #555;--input-bg:#444;--input-txt:#fff;}";
  else if(g_theme_id == 7) cv=":root{--bg-grad:linear-gradient(135deg,#0f2912 0%,#581313 100%);--card-bg:rgba(25,35,25,0.75);--border:1px solid rgba(255,255,255,0.12);--text:#f8fafc;--text-sec:#cbd5e1;--head-col:transparent;--head-txt:linear-gradient(90deg,#ef4444,#22c55e);--bor-top:1px solid rgba(255,255,255,0.35);--bor-right:1px solid rgba(255,255,255,0.35);--input-bg:rgba(255,255,255,0.1);--input-txt:#fff;}";
  else cv=":root{--bg-grad:linear-gradient(135deg,#0f172a 0%,#1e293b 100%);--card-bg:rgba(30,41,59,0.7);--border:1px solid rgba(255,255,255,0.1);--text:#f8fafc;--text-sec:#94a3b8;--head-col:transparent;--head-txt:linear-gradient(90deg,#3b82f6,#8b5cf6);--bor-top:1px solid rgba(255,255,255,0.4);--bor-right:1px solid rgba(255,255,255,0.4);--input-bg:rgba(255,255,255,0.1);--input-txt:#fff;}";
  String h = R"rawliteral(<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Topband V142</title><style>)rawliteral" + cv + R"rawliteral(body{font-family:'Segoe UI',sans-serif;background:var(--bg-grad);color:var(--text);margin:0;padding:10px;min-height:100vh}input:not([type='checkbox']),select{padding:8px;border-radius:4px;border:none;width:100%;margin-bottom:5px;background:var(--input-bg);color:var(--input-txt)}input[type='checkbox']{width:auto;margin-right:5px;cursor:pointer}select option{background-color:#222;color:#fff}.check-lbl{display:flex;align-items:center;padding:5px;cursor:pointer;font-size:0.9rem}.check-lbl:hover{background:rgba(255,255,255,0.05)}.grid{display:grid;gap:15px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}.card{background:var(--card-bg);border:var(--border);border-top:var(--bor-top);border-right:var(--bor-right);border-radius:12px;padding:15px;backdrop-filter:blur(15px);box-shadow:0 4px 6px rgba(0,0,0,0.2)}.pill{padding:4px 8px;border-radius:12px;font-size:0.75rem;font-weight:700;text-transform:uppercase}.p-blue{background:rgba(59,130,246,0.2);color:#60a5fa}.p-green{background:rgba(16,185,129,0.2);color:#34d399}.p-orange{background:rgba(245,158,11,0.2);color:#fbbf24}.p-red{background:rgba(239,68,68,0.2);color:#f87171}.btn{background:rgba(255,255,255,0.1);color:var(--text);border:var(--border);padding:6px 12px;border-radius:6px;cursor:pointer}.drag-handle{cursor:grab;padding:5px;background:rgba(255,255,255,0.05);margin-bottom:10px;display:none;border-radius:4px;text-align:center}body.theme-custom .drag-handle{display:block}.list-item{background:var(--card-bg);border:var(--border);border-radius:8px;overflow:hidden}.list-head-grid{display:grid;grid-template-columns:auto 1fr auto;gap:1rem;align-items:center;padding:10px;cursor:pointer;background:rgba(0,0,0,0.2)}.stat-row{display:flex;gap:15px;font-size:0.85rem;flex-wrap:wrap;justify-content:flex-end}.big-num{font-size:1.8rem;font-weight:700}.sub-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;text-align:center}.box{background:rgba(0,0,0,0.2);padding:8px;border-radius:6px}.stat-box{border-radius:8px;padding:5px}.lbl{font-size:0.7rem;color:var(--text-sec);text-transform:uppercase}.c-grid{display:flex;flex-wrap:wrap;gap:4px;margin-top:10px;justify-content:center}.c-box{background:rgba(255,255,255,0.05);padding:4px;text-align:center;border-radius:4px;font-size:0.75rem;min-width:32px;flex:0 0 56px}.c-min{box-shadow:inset 0 0 0 1px #3b82f6}.c-max{box-shadow:inset 0 0 0 1px #ef4444}.footer{text-align:center;margin-top:30px;font-size:0.8rem;opacity:0.5}canvas{width:100%;height:220px}details{margin-top:10px}#headClock{font-family:'Courier New',monospace;font-size:1.1rem}#sysCard{grid-column:1 / -1;margin-bottom:20px;background:var(--head-col)!important}.header-txt{font-size:1.4rem;font-weight:700;background:var(--head-txt);-webkit-background-clip:text;-webkit-text-fill-color:transparent;color:var(--text)}.bms-border{border-left:5px solid transparent}.file-tbl{width:100%;font-size:0.8rem;border-collapse:collapse;margin-top:10px}.file-tbl td{padding:5px;border-bottom:1px solid rgba(255,255,255,0.1)}.file-tbl button{padding:2px 8px;font-size:0.7rem}.c-lbl{display:block;font-size:0.6rem;color:#aaa;margin-bottom:2px}.field-lbl{display:block;font-size:0.72rem;color:var(--text-sec);margin:0 0 4px 1px}.inp-sm{max-width:180px}.glow-chg{box-shadow:inset 0 20px 20px -10px rgba(16,185,129,0.3)!important}.glow-dis{box-shadow:inset 0 20px 20px -10px rgba(245,158,11,0.3)!important}.glow-idle{box-shadow:inset 0 20px 20px -10px rgba(59,130,246,0.3)!important}.glow-err{box-shadow:inset 0 20px 20px -10px rgba(239,68,68,0.3)!important}.theme-live #sysCard{background:transparent!important;box-shadow:none;border:none;backdrop-filter:none}.theme-live .stat-box{background:rgba(0,0,0,0.6);box-shadow:0 4px 6px rgba(0,0,0,0.3)}.theme-live .sub-grid .box{background:rgba(0,0,0,0.6)}.theme-live details{background:rgba(0,0,0,0.6);border-radius:12px;padding:10px}.theme-cyber .card{border:1px solid #0ff!important;box-shadow:0 0 16px rgba(0,255,255,0.25)}.theme-cyber .btn,.theme-cyber input,.theme-cyber select{border:1px solid #0ff}.theme-cyber .header-txt{background:none;-webkit-text-fill-color:#0ff;color:#0ff}.theme-custom .grid{display:block;position:relative}.theme-custom .card{position:relative;margin-bottom:10px;resize:both;overflow:auto;min-width:300px;min-height:100px}</style></head><body><div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;flex-wrap:wrap;gap:10px;"><div class="header-txt">Topband V142 Master</div><div id="headClock">--:--:--</div><div><button class="btn" id="editBtn" style="display:none;" onclick="toggleEdit()">Edit</button><button class="btn" onclick="toggleView()" id="btnView">LIST</button><button class="btn" onclick="toggleLang()" id="btnLang">DE</button></div></div>
<div id="canErr" style="border:1px solid #ef4444;color:#ef4444;display:none;margin-bottom:15px;background:rgba(239,68,68,0.1);padding:10px;border-radius:8px;text-align:center">CAN BUS ERROR</div><div id="sysErr" style="border:1px solid #ef4444;color:#ef4444;display:none;margin-bottom:15px;background:rgba(239,68,68,0.1);padding:10px;border-radius:8px;text-align:center">SYSTEM ERROR (SD?)</div><div id="safeErr" style="border:1px solid #f59e0b;color:#f59e0b;display:none;margin-bottom:15px;background:rgba(245,158,11,0.1);padding:10px;border-radius:8px;text-align:center">SAFETY CUTOFF ACTIVE!</div>
<div class="grid" id="mainGrid"><div class="card draggable" id="sysCard" draggable="false"><div class="drag-handle">Drag System</div><div style="display:flex;justify-content:space-between;margin-bottom:15px;" id="sysHead"><span style="font-weight:bold;" id="l-sys">SYSTEM TOTAL</span><span class="pill p-blue" id="sysState">IDLE</span></div>
<div style="display:flex;flex-wrap:wrap;justify-content:space-around;text-align:center;gap:15px;"><div class="stat-box"><div class="big-num" id="t-soc" style="color:#10b981;">-- %</div><div class="lbl" id="l-soc">SOC</div></div><div class="stat-box"><div class="big-num" id="t-pwr">-- W</div><div class="lbl" id="l-pwr">POWER</div></div><div class="stat-box"><div class="big-num" id="t-cur">-- A</div><div class="lbl" id="l-cur">CURRENT</div></div><div class="stat-box"><div class="big-num" id="t-vol">-- V</div><div class="lbl" id="l-vol">VOLTAGE</div></div></div>
<div class="sub-grid"><div class="box"><div class="lbl" id="l-mix">SOH / Temp</div><div id="t-mix">--</div></div><div class="box"><div class="lbl" id="l-cap">Capacity</div><div id="t-cap">--</div></div><div class="box"><div class="lbl" id="l-cnt">Active BMS</div><div id="t-cnt">--</div></div><div class="box"><div class="lbl" id="l-ein">Energy In</div><div id="e-in" style="color:#10b981;">--</div></div><div class="box"><div class="lbl" id="l-eout">Energy Out</div><div id="e-out" style="color:#f59e0b;">--</div></div></div>
<details open><summary style="cursor:pointer;color:#94a3b8;" id="l-show">Graph</summary><div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;"><span id="chartTitle" style="font-size:0.8rem;color:#aaa;">POWER (48h)</span><button class="btn" style="padding:2px 8px;font-size:0.7rem;" onclick="toggleChartMode()">48h / 7D</button></div><canvas id="chart"></canvas></details></div><div id="bmsCon" style="display:contents;"></div></div>
<details class="card" style="margin-top:20px;"><summary id="l-set">Settings & Tools</summary><form action="/save" method="POST" style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;">
<div style="grid-column:1/-1;font-weight:bold;margin-top:10px;" id="l-general">GENERAL</div><div><label class="field-lbl" id="l-bms_count">BMS Count</label><input type="number" name="cnt" id="i_cnt" class="inp-sm"></div><div><label class="field-lbl" id="l-cell_cfg">Cells (0=Auto)</label><input type="number" name="cells" id="i_cells" class="inp-sm"></div><select name="theme" id="i_theme" style="grid-column:1/-1;"><option value="0">Modern Glass (Default)</option><option value="1">Retro Dark</option><option value="2">Simple High-Contrast</option><option value="3">Battery Live</option><option value="4">Cyberpunk HUD</option><option value="5">Soft UI</option><option value="6">Custom Dashboard</option><option value="7">Christmas</option></select><div><label class="field-lbl" id="l-ntp">NTP Server</label><input type="text" name="ntp_svr" id="i_ntp"></div><div><label class="field-lbl" id="l-tz">GMT Offset</label><input type="number" name="tz_off" id="i_tz" class="inp-sm"></div><div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="easy" id="i_easy"> <span id="l-easy">Easy Mode (Auto from BMS)</span></label></div><div><label class="field-lbl" id="l-soc-mode">SOC Source</label><select name="soc_mode" id="i_soc_mode" class="inp-sm"><option value="2" id="l-soc-hybrid">Hybrid (Default)</option><option value="1" id="l-soc-raw">Raw BMS</option><option value="0" id="l-soc-calc">Calculated</option></select></div><div><label class="field-lbl" id="l-maint-v">Maintenance Target (V)</label><input type="number" step="0.1" name="maint_v" id="i_maint_v" class="inp-sm"></div><div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="maint_en" id="i_maint_en"> <span id="l-maint-en">Maintenance Charge Mode</span></label></div><div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="ab_en" id="i_ab_en"> <span id="l-ab-en">Auto Balance Watchdog (30d)</span></label></div>
<div style="grid-column:1/-1;font-weight:bold;margin-top:10px;" id="l-network">NETWORK</div><div style="grid-column:1/-1;display:flex;gap:15px;flex-wrap:wrap;"><label class="check-lbl"><input type="checkbox" name="vic_en" id="i_vic_en"> <span id="l-vic">Enable Victron/Pylon CAN</span></label><label class="check-lbl"><input type="checkbox" name="mq_en" id="i_mq_en"> <span id="l-mq">Enable MQTT</span></label><label class="check-lbl"><input type="checkbox" name="mq_full" id="i_mq_full"> <span id="l-mqfull">Full Data</span></label><label class="check-lbl"><input type="checkbox" name="ha_en" id="i_ha_en"> <span id="l-ha">HA Auto-Discovery</span></label></div><div style="grid-column:1/-1;display:flex;gap:8px;flex-wrap:wrap;"><button type="button" class="btn" id="l-ha-send" onclick="haDiscoverySend()">HA Discovery senden</button><button type="button" class="btn" id="l-ha-clear" onclick="haDiscoveryClear()">HA Discovery aufraeumen</button></div><select name="can_proto" id="i_can_proto" style="grid-column:1/-1;"><option value="0">Topband / Victron</option><option value="1">Pylontech Strict</option><option value="2">SMA Sunny Island</option></select><div><label class="field-lbl" id="l-mq-ip">MQTT Broker IP</label><input type="text" name="mq_ip" id="i_mq_ip"></div><div><label class="field-lbl" id="l-mq-port">Port</label><input type="number" name="mq_pt" id="i_mq_pt" class="inp-sm"></div><div><label class="field-lbl" id="l-mq-user">User</label><input type="text" name="mq_us" id="i_mq_us"></div><div><label class="field-lbl" id="l-mq-pass">Password</label><input type="password" name="mq_pw" id="i_mq_pw"></div>
<div style="grid-column:1/-1;font-weight:bold;margin-top:10px;" id="l-logging">LOGGING</div><div style="grid-column:1/-1;display:flex;gap:15px;flex-wrap:wrap;"><label class="check-lbl"><input type="checkbox" name="sd_en" id="i_sd"> <span id="l-sd">Enable SD Logging</span></label><label class="check-lbl"><input type="checkbox" name="debug" id="i_debug"> <span id="l-debug">Enable Debug</span></label><label class="check-lbl" style="color:#ef4444;"><input type="checkbox" name="spy" id="i_spy"> <span id="l-spy">Enable Raw Spy</span></label></div><div id="sd_section" style="grid-column:1/-1;"><div style="font-size:0.8rem;color:#aaa;" id="sd_stat">Checking SD...</div><div id="sd_hint" style="font-size:0.75rem;color:#ef4444;margin-bottom:10px;">Format: FAT32 (Max 32GB)</div><table class="file-tbl" id="fileTable"></table><div style="margin-top:10px;"><button type="button" class="btn" style="background:#ef4444;" id="btn-wipe" onclick="if(confirm(T('confirm_wipe'))) fetch('/sd/clear?all=1').then(()=>location.reload())">WIPE SD</button></div></div>
<div style="grid-column:1/-1;font-weight:bold;margin-top:10px;color:#f59e0b;" id="l-profi">PRO MODE</div>
<div class="box" style="margin-bottom:10px;text-align:left;grid-column:1/-1;"><div style="font-weight:bold;border-bottom:1px solid #555;margin-bottom:5px;">AKTIVE GRENZWERTE (Info):</div><div style="display:grid;grid-template-columns:1fr 1fr;gap:5px;font-size:0.8rem;">
<div>Laden: <span id="v_chg" style="color:#10b981">--</span></div>
<div>Entladen: <span id="v_dis" style="color:#f59e0b">--</span></div>
<div>Temp Charge: <span id="v_t_chg">--</span></div>
<div>Temp Dischg: <span id="v_t_dis">--</span></div>
</div></div>
<label style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="exp" id="chk_exp" onclick="toggleExp(this)"> <span id="l-exp">Enable Expert Mode</span></label></label><div id="exp_box" style="display:none;border:1px solid #f59e0b;padding:10px;border-radius:5px;background:rgba(245,158,11,0.1);margin-top:10px;"><div style="color:#f59e0b;font-size:0.8rem;">CURRENT LIMITS:</div><div style="display:grid;grid-template-columns:1fr 1fr;gap:5px;"><label>Charge (A)</label><input type="number" step="0.1" name="chg" id="i_chg"><label>Discharge (A)</label><input type="number" step="0.1" name="dis" id="i_dis"><label>Charge (V)</label><input type="number" step="0.01" name="cvl" id="i_cvl"></div><div style="color:#ef4444;font-size:0.8rem;margin-top:10px;">THERMOSTAT CUTOFF (&deg;C):</div><div style="display:grid;grid-template-columns:1fr 1fr;gap:5px;"><div><label>Chg Min</label><input type="number" step="0.1" name="t_c_min" id="i_tcmin"></div><div><label>Chg Max</label><input type="number" step="0.1" name="t_c_max" id="i_tcmax"></div><div><label>Dis Min</label><input type="number" step="0.1" name="t_d_min" id="i_tdmin"></div><div><label>Dis Max</label><input type="number" step="0.1" name="t_d_max" id="i_tdmax"></div><div style="grid-column:1/-1;"><label>Logic</label><select name="t_mode" id="i_tmode"><option value="0">MAX SAFETY</option><option value="1">AVERAGE</option></select></div></div><div style="color:#ef4444;font-size:0.8rem;margin-top:10px;">SAFETY CUTOFF:</div><div style="display:grid;grid-template-columns:1fr 1fr;gap:5px;"><div><label>Max Pack (V)</label><input type="number" step="0.01" name="s_vol" id="i_svol"></div><div><label>Max Cell (V)</label><input type="number" step="0.01" name="s_cel" id="i_scel"></div><div style="grid-column:1/-1;"><label>Max Cell Drift (V)</label><input type="number" step="0.01" name="s_drift" id="i_sdrift"></div></div></div><button type="submit" class="btn" style="grid-column:1/-1;background:#10b981;margin-top:10px;" id="l-save">SAVE REBOOT</button></form><div class="help-sec" style="font-size:0.8rem;color:#aaa;margin-top:10px;"><b id="l-led">Status LED:</b> BLUE (Boot) | GREEN (WiFi) | RED (AP Mode)</div><div class="help-sec" style="font-size:0.78rem;color:#94a3b8;margin-top:6px;"><span id="l-thx">THX an</span> <a href="https://github.com/linedot/topband-bms" target="_blank" rel="noopener" style="color:#60a5fa;">linedot/topband-bms</a></div><div id="log" style="font-family:monospace;font-size:0.7rem;height:100px;overflow-y:scroll;background:rgba(0,0,0,0.3);padding:5px;margin-top:10px;"></div></details>
<details class="card" style="margin-top:20px;"><summary id="l-svc">BMS Service / Diagnose</summary><div style="margin-top:10px;display:grid;grid-template-columns:1fr 1fr;gap:10px;"><div><label class="field-lbl" id="l-svc-bms">BMS ID</label><select id="svc_bms"></select></div><div><label class="field-lbl" id="l-svc-cellbuf">Buffer Cell (mV)</label><input type="number" id="svc_buf_cell_mv" value="20" step="1"></div><div><label class="field-lbl" id="l-svc-packbuf">Buffer Pack (V)</label><input type="number" id="svc_buf_pack_v" value="0.3" step="0.01"></div><div><label class="field-lbl" id="l-svc-tempbuf">Buffer Temp (C)</label><input type="number" id="svc_buf_temp_c" value="2" step="0.1"></div><div><label class="field-lbl" id="l-svc-currbuf">Buffer Current (%)</label><input type="number" id="svc_buf_curr_pct" value="10" step="1"></div><div style="display:flex;align-items:flex-end;gap:8px;flex-wrap:wrap;"><button type="button" class="btn" id="l-svc-read" onclick="svcRead()">Read</button><button type="button" class="btn" id="l-svc-settime" onclick="svcSetTime()">Set Time</button><button type="button" class="btn" id="l-svc-apply" onclick="svcApplyAuto()">Apply AutoCfg</button></div></div><div id="svc_stat" style="margin-top:10px;font-size:0.85rem;color:#94a3b8;">Ready</div><pre id="svc_out" style="margin-top:10px;background:rgba(0,0,0,0.3);padding:10px;border-radius:8px;max-height:320px;overflow:auto;font-size:0.75rem;"></pre></details>
<script>
let view='grid';let lang='de';let chartMode='power';let initDone=false;let editMode=false;let svcLast=null;let setupWizardShown=false;let svcUiInit=false;const ctx=document.getElementById('chart').getContext('2d');
const TR={
de:{
'l-sys':'SYSTEM GESAMT','l-soc':'KAPAZITAET','l-pwr':'LEISTUNG','l-cur':'STROM','l-vol':'SPANNUNG','l-mix':'SOH / TEMP','l-cap':'KAPAZITAET','l-cnt':'AKTIVE BMS','l-ein':'ENERGIE EIN','l-eout':'ENERGIE AUS','l-show':'Diagramm','l-set':'Einstellungen & Tools',
'l-general':'ALLGEMEIN','l-network':'NETZWERK','l-logging':'LOGGING','l-profi':'PROFI MODUS','l-save':'SPEICHERN & NEUSTART','l-led':'Status LED:',
'l-vic':'Victron/Pylon CAN aktivieren','l-mq':'MQTT aktivieren','l-mqfull':'Volle Daten','l-ha':'HA Auto-Discovery','l-ha-send':'HA Discovery senden','l-ha-clear':'HA Discovery aufraeumen','l-sd':'SD Logging aktivieren','l-debug':'Debug aktivieren','l-spy':'Raw Spy aktivieren','l-exp':'Expertenmodus aktivieren','l-easy':'Easy Mode (Auto aus BMS)','l-soc-mode':'SOC Quelle','l-soc-hybrid':'Hybrid (Standard)','l-soc-raw':'Raw BMS','l-soc-calc':'Berechnet','l-maint-en':'Maintenance-Lademodus','l-maint-v':'Maintenance Ziel (V)','l-ab-en':'Auto-Balance Watchdog (30 Tage)','l-bms_count':'BMS Anzahl','l-cell_cfg':'Zellen (0=Auto)','l-ntp':'NTP Server','l-tz':'GMT Offset','l-mq-ip':'MQTT Broker IP','l-mq-port':'MQTT Port','l-mq-user':'MQTT Benutzer','l-mq-pass':'MQTT Passwort','l-thx':'THX an',
'chart_power':'LEISTUNG (48h)','chart_energy':'ENERGIE (7 Tage)','today':'HEUTE','status':'Status','ok':'OK','no_card':'KEINE KARTE','sys_error':'SYSTEMFEHLER (SD?)','safe_cutoff':'SICHERHEITSABSCHALTUNG AKTIV!',
'can_error':'CAN FEHLER','sd_write_error':'SD SCHREIBFEHLER','confirm_expert':'EXPERTENMODUS WARNUNG!','confirm_wipe':'ALLE LOGS LOESCHEN?','no_files':'Keine Dateien gefunden.',
'download':'DL','delete':'DEL','delete_file':'Datei loeschen','bms':'BMS','diff':'Diff','soh':'SOH','cells':'Zellen','wipe_sd':'SD LOESCHEN',
'l-svc':'BMS Service / Diagnose','l-svc-bms':'BMS ID','l-svc-cellbuf':'Buffer Zelle (mV)','l-svc-packbuf':'Buffer Pack (V)','l-svc-tempbuf':'Buffer Temp (C)','l-svc-currbuf':'Buffer Strom (%)','l-svc-read':'Lesen','l-svc-settime':'Uhr setzen','l-svc-apply':'AutoCfg uebernehmen',
'svc_ready':'Bereit','svc_reading':'Lese Service-Daten ...','svc_read_ok':'Service-Daten geladen','svc_read_fail':'Service-Lesen fehlgeschlagen','svc_set':'Setze BMS-Uhr ...','svc_set_ok':'BMS-Uhr aktualisiert','svc_set_fail':'Uhr setzen fehlgeschlagen','svc_apply':'Wende Auto-Config an ...','svc_apply_ok':'Auto-Config angewendet','svc_apply_fail':'Auto-Config fehlgeschlagen','ha_send_ok':'HA Discovery gesendet','ha_send_fail':'HA Discovery senden fehlgeschlagen','ha_clear_ok':'HA Discovery aufgeraeumt','ha_clear_fail':'HA Discovery aufraeumen fehlgeschlagen','svc_apply_hint':'Easy-Wizard hat Laufzeitwerte gesetzt. Bitte SPEICHERN & NEUSTART fuer Persistenz.','wiz_title':'Ersteinrichtung','wiz_text':'Wizard: Protokoll/MQTT setzen. Batterie-Schutzwerte bleiben feste Standardwerte, ausser im Expertenmodus.','wiz_auto':'Standard (Fixwerte)','wiz_reco':'Standard (Fixwerte)','wiz_expert':'Experte (manuell)','wiz_bms':'Wie viele BMS?','wiz_cells_q':'Zellenzahl (0=Auto)','wiz_mqtt':'MQTT nutzen?','wiz_mq_ip':'MQTT Broker IP','wiz_mq_port':'MQTT Port','wiz_mq_user':'MQTT Benutzer','wiz_mq_pass':'MQTT Passwort','wiz_apply':'Wizard anwenden','wiz_skip_btn':'Wizard ueberspringen','wiz_done_auto':'Auto-Profil gesetzt. Bitte SPEICHERN & NEUSTART.','wiz_done_reco':'Standardprofil gesetzt. Bitte SPEICHERN & NEUSTART.','wiz_done_expert':'Expertenprofil gesetzt. Bitte SPEICHERN & NEUSTART.','wiz_skip':'Wizard uebersprungen. Du kannst Expert Mode oder manuell nutzen.'
},
en:{
'l-sys':'SYSTEM TOTAL','l-soc':'SOC','l-pwr':'POWER','l-cur':'CURRENT','l-vol':'VOLTAGE','l-mix':'SOH / TEMP','l-cap':'CAPACITY','l-cnt':'ACTIVE BMS','l-ein':'ENERGY IN','l-eout':'ENERGY OUT','l-show':'Graph','l-set':'Settings & Tools',
'l-general':'GENERAL','l-network':'NETWORK','l-logging':'LOGGING','l-profi':'PRO MODE','l-save':'SAVE & REBOOT','l-led':'Status LED:',
'l-vic':'Enable Victron/Pylon CAN','l-mq':'Enable MQTT','l-mqfull':'Full Data','l-ha':'HA Auto-Discovery','l-ha-send':'Send HA Discovery','l-ha-clear':'Clear HA Discovery','l-sd':'Enable SD Logging','l-debug':'Enable Debug','l-spy':'Enable Raw Spy','l-exp':'Enable Expert Mode','l-easy':'Easy Mode (Auto from BMS)','l-soc-mode':'SOC Source','l-soc-hybrid':'Hybrid (Default)','l-soc-raw':'Raw BMS','l-soc-calc':'Calculated','l-maint-en':'Maintenance Charge Mode','l-maint-v':'Maintenance Target (V)','l-ab-en':'Auto Balance Watchdog (30 days)','l-bms_count':'BMS Count','l-cell_cfg':'Cells (0=Auto)','l-ntp':'NTP Server','l-tz':'GMT Offset','l-mq-ip':'MQTT Broker IP','l-mq-port':'MQTT Port','l-mq-user':'MQTT User','l-mq-pass':'MQTT Password','l-thx':'THX to',
'chart_power':'POWER (48h)','chart_energy':'ENERGY (7 Days)','today':'TODAY','status':'Status','ok':'OK','no_card':'NO CARD','sys_error':'SYSTEM ERROR (SD?)','safe_cutoff':'SAFETY CUTOFF ACTIVE!',
'can_error':'CAN ERROR','sd_write_error':'SD WRITE ERROR','confirm_expert':'EXPERT MODE WARNING!','confirm_wipe':'DELETE ALL LOGS?','no_files':'No files found.',
'download':'DL','delete':'DEL','delete_file':'Delete file','bms':'BMS','diff':'Diff','soh':'SOH','cells':'Cells','wipe_sd':'WIPE SD',
'l-svc':'BMS Service / Diagnostics','l-svc-bms':'BMS ID','l-svc-cellbuf':'Buffer Cell (mV)','l-svc-packbuf':'Buffer Pack (V)','l-svc-tempbuf':'Buffer Temp (C)','l-svc-currbuf':'Buffer Current (%)','l-svc-read':'Read','l-svc-settime':'Set Time','l-svc-apply':'Apply AutoCfg',
'svc_ready':'Ready','svc_reading':'Reading service data ...','svc_read_ok':'Service data loaded','svc_read_fail':'Service read failed','svc_set':'Setting BMS time ...','svc_set_ok':'BMS time updated','svc_set_fail':'Time set failed','svc_apply':'Applying auto config ...','svc_apply_ok':'Auto config applied','svc_apply_fail':'Auto config failed','ha_send_ok':'HA discovery sent','ha_send_fail':'HA discovery send failed','ha_clear_ok':'HA discovery cleared','ha_clear_fail':'HA discovery clear failed','svc_apply_hint':'Easy wizard applied runtime values. Please SAVE REBOOT to persist.','wiz_title':'First setup','wiz_text':'Wizard sets protocol/MQTT. Battery protection values stay fixed defaults unless Expert mode is enabled.','wiz_auto':'Standard (fixed)','wiz_reco':'Standard (fixed)','wiz_expert':'Expert (manual)','wiz_bms':'How many BMS?','wiz_cells_q':'Cell count (0=Auto)','wiz_mqtt':'Use MQTT?','wiz_mq_ip':'MQTT Broker IP','wiz_mq_port':'MQTT Port','wiz_mq_user':'MQTT User','wiz_mq_pass':'MQTT Password','wiz_apply':'Apply wizard','wiz_skip_btn':'Skip wizard','wiz_done_auto':'Auto profile applied. Please SAVE REBOOT.','wiz_done_reco':'Standard profile applied. Please SAVE REBOOT.','wiz_done_expert':'Expert profile selected. Please SAVE REBOOT.','wiz_skip':'Wizard skipped. You can use Expert Mode or manual settings.'
}
};
function T(k){const d=TR[lang]||TR.en;return (d&&d[k])?d[k]:(TR.en[k]||k);}
TR.de.wiz_proto='Wechselrichter/Profil';
TR.de.wiz_proto_victron='Victron / Standard CAN';
TR.de.wiz_proto_pylon='Pylontech Strict';
TR.de.wiz_proto_sma='SMA Sunny Island';
TR.de.wiz_proto_opendtu='OpenDTU / MQTT Only';
TR.de.wiz_save_hint='Wichtig: Einstellungen werden erst nach SPEICHERN & NEUSTART dauerhaft uebernommen.';
TR.en.wiz_proto='Inverter/Profile';
TR.en.wiz_proto_victron='Victron / Standard CAN';
TR.en.wiz_proto_pylon='Pylontech Strict';
TR.en.wiz_proto_sma='SMA Sunny Island';
TR.en.wiz_proto_opendtu='OpenDTU / MQTT only';
TR.en.wiz_save_hint='Important: settings are only persisted after SAVE REBOOT.';
const ST_TXT={de:{'IDLE':'STANDBY','CHG':'LADEN','DIS':'ENTLADEN','OFF':'OFF'},en:{'IDLE':'IDLE','CHG':'CHARGING','DIS':'DISCHARGING','OFF':'OFF'}};
function updateClock(){document.getElementById('headClock').innerText=new Date().toLocaleTimeString()}setInterval(updateClock,1000);
function updateText(){const b=document.getElementById('btnLang');if(b)b.innerText=lang.toUpperCase();const t=TR[lang];for(const k in t){const e=document.getElementById(k);if(e)e.innerHTML=t[k];}const ct=document.getElementById('chartTitle');if(ct)ct.innerText=(chartMode==='power')?T('chart_power'):T('chart_energy');const ce=document.getElementById('canErr');if(ce)ce.innerText=T('can_error');const se=document.getElementById('sysErr');if(se)se.innerText=T('sys_error');const sf=document.getElementById('safeErr');if(sf)sf.innerText=T('safe_cutoff');const wb=document.getElementById('btn-wipe');if(wb)wb.innerText=T('wipe_sd');const ss=document.getElementById('svc_stat');if(ss&&!svcLast)ss.innerText=T('svc_ready');}
function toggleLang(){lang=(lang==='de'?'en':'de');updateText();}
function toggleDetails(el){if(!editMode){let det=el.parentElement.querySelector('.b-det');if(det)det.style.display=(det.style.display==='none'?'block':'none')}}
function toggleChartMode(){chartMode=(chartMode==='power')?'energy':'power';document.getElementById('chartTitle').innerText=(chartMode==='power')?T('chart_power'):T('chart_energy');update()}
function toggleExp(el){let box=document.getElementById('exp_box');if(el.checked){if(confirm(T('confirm_expert'))){box.style.display='block'}else{el.checked=false;box.style.display='none'}}else{box.style.display='none'}}
function applyRecommendedProfile(){
  const setVal=(id,val)=>{const el=document.getElementById(id);if(el)el.value=val;};
  const setChk=(id,val)=>{const el=document.getElementById(id);if(el)el.checked=!!val;};
  setChk('i_easy',false);
  setVal('i_chg','30.0');
  setVal('i_dis','30.0');
  setVal('i_cvl','52.5');
  setVal('i_svol','53.25');
  setVal('i_scel','3.55');
  setVal('i_sdrift','0.20');
  setVal('i_tcmin','5.0');
  setVal('i_tcmax','50.0');
  setVal('i_tdmin','-20.0');
  setVal('i_tdmax','60.0');
  setVal('i_tmode','0');
  setVal('i_soc_mode','2');
  setChk('i_maint_en',false);
  setChk('i_ab_en',true);
  setVal('i_maint_v','51.0');
}
function showSetupWizardDialog(){
  const old=document.getElementById('setupWizardOverlay'); if(old) old.remove();
  const ov=document.createElement('div');
  ov.id='setupWizardOverlay';
  ov.style.cssText='position:fixed;inset:0;background:rgba(0,0,0,0.60);z-index:9999;display:flex;align-items:center;justify-content:center;padding:16px;';
  const card=document.createElement('div');
  card.style.cssText='max-width:620px;width:100%;background:#0f172a;border:1px solid rgba(148,163,184,0.35);border-radius:14px;padding:18px;box-shadow:0 12px 30px rgba(0,0,0,0.45);';

  const curCnt = document.getElementById('i_cnt')?.value || '2';
  const curCells = document.getElementById('i_cells')?.value || '0';
  const curExpert = !!document.getElementById('chk_exp')?.checked;
  const curMode = curExpert ? 'expert' : 'reco';
  const curMq = !!document.getElementById('i_mq_en')?.checked;
  const curMqIp = document.getElementById('i_mq_ip')?.value || '';
  const curMqPt = document.getElementById('i_mq_pt')?.value || '1883';
  const curMqUs = document.getElementById('i_mq_us')?.value || '';
  const curMqPw = document.getElementById('i_mq_pw')?.value || '';
  const curVic = !!document.getElementById('i_vic_en')?.checked;
  const curCanProto = parseInt(document.getElementById('i_can_proto')?.value || '0', 10);
  let curProto = 'victron';
  if(!curVic) curProto = 'open_dtu';
  else if(curCanProto === 1) curProto = 'pylon';
  else if(curCanProto === 2) curProto = 'sma';

  card.innerHTML = `
    <div style="font-size:1rem;font-weight:700;margin-bottom:6px;">${T('wiz_title')}</div>
    <div style="font-size:0.92rem;color:#cbd5e1;margin-bottom:12px;">${T('wiz_text')}</div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;">
      <div style="grid-column:1 / -1;display:flex;gap:12px;flex-wrap:wrap;">
        <label class="check-lbl" style="padding:0;"><input type="radio" name="wiz_mode" id="wiz_mode_auto" ${curMode==='auto'?'checked':''}> ${T('wiz_auto')}</label>
        <label class="check-lbl" style="padding:0;"><input type="radio" name="wiz_mode" id="wiz_mode_reco" ${curMode==='reco'?'checked':''}> ${T('wiz_reco')}</label>
        <label class="check-lbl" style="padding:0;"><input type="radio" name="wiz_mode" id="wiz_mode_expert" ${curMode==='expert'?'checked':''}> ${T('wiz_expert')}</label>
      </div>
      <div style="grid-column:1 / -1;">
        <label class="field-lbl">${T('wiz_proto')}</label>
        <select id="wiz_proto">
          <option value="victron" ${curProto==='victron'?'selected':''}>${T('wiz_proto_victron')}</option>
          <option value="pylon" ${curProto==='pylon'?'selected':''}>${T('wiz_proto_pylon')}</option>
          <option value="sma" ${curProto==='sma'?'selected':''}>${T('wiz_proto_sma')}</option>
          <option value="open_dtu" ${curProto==='open_dtu'?'selected':''}>${T('wiz_proto_opendtu')}</option>
        </select>
      </div>
      <div><label class="field-lbl">${T('wiz_bms')}</label><input id="wiz_cnt" type="number" min="1" max="16" value="${curCnt}"></div>
      <div><label class="field-lbl">${T('wiz_cells_q')}</label><input id="wiz_cells" type="number" min="0" max="32" value="${curCells}"></div>
      <div style="grid-column:1 / -1;"><label class="check-lbl" style="padding:0;"><input type="checkbox" id="wiz_mqtt_en" ${curMq?'checked':''}> ${T('wiz_mqtt')}</label></div>
      <div><label class="field-lbl">${T('wiz_mq_ip')}</label><input id="wiz_mq_ip" type="text" value="${curMqIp}"></div>
      <div><label class="field-lbl">${T('wiz_mq_port')}</label><input id="wiz_mq_pt" type="number" min="1" max="65535" value="${curMqPt}"></div>
      <div><label class="field-lbl">${T('wiz_mq_user')}</label><input id="wiz_mq_us" type="text" value="${curMqUs}"></div>
      <div><label class="field-lbl">${T('wiz_mq_pass')}</label><input id="wiz_mq_pw" type="password" value="${curMqPw}"></div>
    </div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:14px;">
      <button type="button" id="wiz_apply" class="btn" style="background:#0ea5e9;border:1px solid #38bdf8;color:#06263a;font-weight:700;">${T('wiz_apply')}</button>
      <button type="button" id="wiz_skip_btn" class="btn" style="background:rgba(255,255,255,0.08);border:1px solid rgba(148,163,184,0.45);color:#e2e8f0;">${T('wiz_skip_btn')}</button>
    </div>
    <div style="margin-top:10px;font-size:0.82rem;color:#cbd5e1;">${T('wiz_save_hint')}</div>
  `;

  ov.appendChild(card); document.body.appendChild(ov);

  const mqToggle = card.querySelector('#wiz_mqtt_en');
  const mqFields = ['wiz_mq_ip','wiz_mq_pt','wiz_mq_us','wiz_mq_pw'].map(id=>card.querySelector('#'+id));
  const syncMq = ()=>{ const on = mqToggle?.checked; mqFields.forEach(el=>{ if(el){ el.disabled=!on; el.style.opacity=on?'1':'0.6'; } }); };
  if(mqToggle) mqToggle.addEventListener('change', syncMq);
  syncMq();

  card.querySelector('#wiz_apply').onclick = ()=>{
    const cnt = parseInt(card.querySelector('#wiz_cnt').value || '2', 10);
    const cells = parseInt(card.querySelector('#wiz_cells').value || '0', 10);
    let mode = 'reco';
    if (card.querySelector('#wiz_mode_expert').checked) mode = 'expert';
    const useMq = !!card.querySelector('#wiz_mqtt_en').checked;
    const mqIp = card.querySelector('#wiz_mq_ip').value || '';
    const mqPt = card.querySelector('#wiz_mq_pt').value || '1883';
    const mqUs = card.querySelector('#wiz_mq_us').value || '';
    const mqPw = card.querySelector('#wiz_mq_pw').value || '';
    const proto = card.querySelector('#wiz_proto').value || 'victron';

    document.getElementById('i_cnt').value = Number.isFinite(cnt) ? Math.max(1, Math.min(16, cnt)) : 2;
    document.getElementById('i_cells').value = Number.isFinite(cells) ? Math.max(0, Math.min(32, cells)) : 0;
    document.getElementById('i_easy').checked = false;
    document.getElementById('i_mq_en').checked = useMq;
    document.getElementById('i_mq_ip').value = mqIp;
    document.getElementById('i_mq_pt').value = mqPt;
    document.getElementById('i_mq_us').value = mqUs;
    document.getElementById('i_mq_pw').value = mqPw;
    if (proto === 'open_dtu') {
      document.getElementById('i_vic_en').checked = false;
      document.getElementById('i_can_proto').value = '0';
    } else {
      document.getElementById('i_vic_en').checked = true;
      if (proto === 'pylon') document.getElementById('i_can_proto').value = '1';
      else if (proto === 'sma') document.getElementById('i_can_proto').value = '2';
      else document.getElementById('i_can_proto').value = '0';
    }
    if (mode === 'expert') {
      document.getElementById('chk_exp').checked = true;
      document.getElementById('exp_box').style.display = 'block';
    } else {
      document.getElementById('chk_exp').checked = false;
      document.getElementById('exp_box').style.display = 'none';
      applyRecommendedProfile();
    }
    svcSyncOptions(parseInt(document.getElementById('i_cnt').value || '2', 10));

    ov.remove();
    if(mode === 'expert'){ svcShow(T('wiz_done_expert')); }
    else { svcShow(T('wiz_done_reco')); }
  };

  card.querySelector('#wiz_skip_btn').onclick = ()=>{
    ov.remove();
    svcShow(T('wiz_skip'));
  };
}
function beautifyServicePanel(){
  if(svcUiInit) return;
  svcUiInit = true;
  ['svc_bms','svc_buf_cell_mv','svc_buf_pack_v','svc_buf_temp_c','svc_buf_curr_pct'].forEach(id=>{
    const el=document.getElementById(id);
    if(el){ el.style.maxWidth='260px'; el.style.width='100%'; }
  });
  const actions=document.getElementById('l-svc-read')?.parentElement;
  if(actions){
    actions.style.gridColumn='1 / -1';
    actions.style.justifyContent='flex-start';
    actions.style.alignItems='center';
    actions.style.paddingTop='2px';
  }
  const stat=document.getElementById('svc_stat');
  if(stat){
    stat.style.background='rgba(15,23,42,0.55)';
    stat.style.border='1px solid rgba(148,163,184,0.24)';
    stat.style.borderRadius='8px';
    stat.style.padding='8px 10px';
  }
}
function loadFiles(){fetch('/sd/list').then(r=>r.json()).then(files=>{let t=document.getElementById('fileTable');t.innerHTML="";if(files.length===0){t.innerHTML=`<tr><td colspan='3'>${T('no_files')}</td></tr>`;return}files.forEach(f=>{let sz=(f.size/1024).toFixed(1)+" KB";t.innerHTML+=`<tr><td>${f.name}</td><td>${sz}</td><td><button type='button' class='btn' onclick="window.open('/sd/download?file=/${f.name}')">${T('download')}</button> <button type='button' class='btn' style='background:#ef4444;' onclick="if(confirm('${T('delete_file')}: ${f.name}?')) fetch('/sd/clear?file=/${f.name}').then(()=>loadFiles())">${T('delete')}</button></td></tr>`})})}
function svcGetId(){const el=document.getElementById('svc_bms');if(!el)return 0;return parseInt(el.value||'0',10)||0}
function svcBufQS(){const c=document.getElementById('svc_buf_cell_mv')?.value||'20';const p=document.getElementById('svc_buf_pack_v')?.value||'0.3';const t=document.getElementById('svc_buf_temp_c')?.value||'2';const k=document.getElementById('svc_buf_curr_pct')?.value||'10';return `buf_cell_mv=${encodeURIComponent(c)}&buf_pack_v=${encodeURIComponent(p)}&buf_temp_c=${encodeURIComponent(t)}&buf_curr_pct=${encodeURIComponent(k)}`}
function svcSyncOptions(cnt){const el=document.getElementById('svc_bms');if(!el||!Number.isFinite(cnt)||cnt<1)return;const cur=el.value;el.innerHTML='';for(let i=0;i<cnt;i++){const o=document.createElement('option');o.value=String(i);o.textContent='BMS '+i;el.appendChild(o)}if(cur!==''&&parseInt(cur,10)<cnt)el.value=cur}
function svcShow(msg,obj){const s=document.getElementById('svc_stat');if(s)s.textContent=msg;const o=document.getElementById('svc_out');if(o)o.textContent=obj?JSON.stringify(obj,null,2):''}
async function svcRead(){const id=svcGetId();svcShow(T('svc_reading'));try{const r=await fetch(`/svc/bms?id=${id}&${svcBufQS()}`);const d=await r.json();svcLast=d;svcShow(d.ok?T('svc_read_ok'):T('svc_read_fail'),d)}catch(e){svcShow(T('svc_read_fail')+': '+e.message)}}
async function svcSetTime(){const id=svcGetId();svcShow(T('svc_set'));try{const r=await fetch(`/svc/bms/time/set?id=${id}`);const d=await r.json();svcShow(d.ok?T('svc_set_ok'):T('svc_set_fail'),d);if(d.ok)svcRead()}catch(e){svcShow(T('svc_set_fail')+': '+e.message)}}
async function svcApplyAuto(){const id=svcGetId();svcShow(T('svc_apply'));try{const r=await fetch(`/svc/autocfg/apply?id=${id}&${svcBufQS()}`);const d=await r.json();if(d.ok&&d.applied){document.getElementById('chk_exp').checked=true;document.getElementById('exp_box').style.display='block';if(d.applied.s_vol!==undefined)document.getElementById('i_svol').value=d.applied.s_vol;if(d.applied.s_cel!==undefined)document.getElementById('i_scel').value=d.applied.s_cel;if(d.applied.t_c_min!==undefined)document.getElementById('i_tcmin').value=d.applied.t_c_min;if(d.applied.t_c_max!==undefined)document.getElementById('i_tcmax').value=d.applied.t_c_max;if(d.applied.t_d_min!==undefined)document.getElementById('i_tdmin').value=d.applied.t_d_min;if(d.applied.t_d_max!==undefined)document.getElementById('i_tdmax').value=d.applied.t_d_max;if(d.applied.chg!==undefined)document.getElementById('i_chg').value=d.applied.chg;if(d.applied.dis!==undefined)document.getElementById('i_dis').value=d.applied.dis;if(d.applied.cvl!==undefined)document.getElementById('i_cvl').value=d.applied.cvl;}svcShow(d.ok?T('svc_apply_ok'):T('svc_apply_fail'),d)}catch(e){svcShow(T('svc_apply_fail')+': '+e.message)}}
async function haDiscoverySend(){
  svcShow(T('svc_reading'));
  try {
    const r = await fetch('/ha/discovery/send');
    const d = await r.json();
    const msg = d.ok ? T('ha_send_ok') : (T('ha_send_fail') + (d.err ? ': ' + d.err : ''));
    svcShow(msg, d);
    alert(msg);
  } catch(e) {
    const msg = T('ha_send_fail') + ': ' + e.message;
    svcShow(msg);
    alert(msg);
  }
}
async function haDiscoveryClear(){
  svcShow(T('svc_reading'));
  try {
    const r = await fetch('/ha/discovery/clear');
    const d = await r.json();
    const msg = d.ok ? T('ha_clear_ok') : (T('ha_clear_fail') + (d.err ? ': ' + d.err : ''));
    svcShow(msg, d);
    alert(msg);
  } catch(e) {
    const msg = T('ha_clear_fail') + ': ' + e.message;
    svcShow(msg);
    alert(msg);
  }
}
let dragSrc=null;function dragStart(e){dragSrc=this;e.dataTransfer.effectAllowed='move';e.dataTransfer.setData('text/html',this.innerHTML);this.style.opacity='0.4'}function dragOver(e){if(e.preventDefault){e.preventDefault()}e.dataTransfer.dropEffect='move';return false}function dragDrop(e){if(dragSrc!=this){let srcHTML=dragSrc.innerHTML;let tgtHTML=this.innerHTML;dragSrc.innerHTML=tgtHTML;this.innerHTML=srcHTML;}return false}function dragEnd(e){this.style.opacity='1'}function enableDrag(){let items=document.querySelectorAll('.draggable');items.forEach(item=>{item.setAttribute('draggable',true);item.addEventListener('dragstart',dragStart);item.addEventListener('dragover',dragOver);item.addEventListener('drop',dragDrop);item.addEventListener('dragend',dragEnd)})}
function toggleEdit(){editMode=!editMode;let btn=document.getElementById('editBtn');btn.style.background=editMode?'#ef4444':'rgba(255,255,255,0.1)';document.body.classList.toggle('edit-active')}
function drawChart(d,ts){if(!d||d.length<2)return;const c=ctx.canvas;c.width=c.clientWidth;c.height=220;const w=c.width,h=c.height,pad=40,bPad=30,tPad=30;ctx.clearRect(0,0,w,h);let min=9999,max=-9999;let allZero=true;for(let i=0;i<d.length;i++){if(d[i]<min)min=d[i];if(d[i]>max)max=d[i];if(d[i]!=0)allZero=false}if(allZero){min=-100;max=100}else{let range=max-min;if(range<10)range=10;min-=range*0.1;max+=range*0.1}let r=max-min;const drawH=h-bPad-tPad;const getY=(v)=>tPad+drawH-((v-min)/r*drawH);const y0=getY(0);ctx.textAlign="right";ctx.textBaseline="middle";ctx.fillStyle="#aaa";ctx.font='10px sans-serif';const steps=4;for(let i=0;i<=steps;i++){let val=min+(r*(i/steps));let y=getY(val);ctx.beginPath();ctx.strokeStyle="rgba(255,255,255,0.05)";ctx.moveTo(pad,y);ctx.lineTo(w,y);ctx.stroke();ctx.fillText(Math.round(val),pad-5,y)}let fillGrd=ctx.createLinearGradient(0,0,0,h-bPad);let zeroRatio=(max-0)/r;if(zeroRatio<0)zeroRatio=0;if(zeroRatio>1)zeroRatio=1;fillGrd.addColorStop(0,'rgba(16, 185, 129, 0.4)');fillGrd.addColorStop(zeroRatio,'rgba(16, 185, 129, 0.1)');fillGrd.addColorStop(zeroRatio,'rgba(245, 158, 11, 0.1)');fillGrd.addColorStop(1,'rgba(245, 158, 11, 0.4)');let strokeGrd=ctx.createLinearGradient(0,0,0,h-bPad);strokeGrd.addColorStop(0,'#10b981');strokeGrd.addColorStop(zeroRatio,'#10b981');strokeGrd.addColorStop(zeroRatio,'#f59e0b');strokeGrd.addColorStop(1,'#f59e0b');ctx.beginPath();ctx.strokeStyle='#666';ctx.lineWidth=1;ctx.setLineDash([5,5]);ctx.moveTo(pad,y0);ctx.lineTo(w,y0);ctx.stroke();ctx.setLineDash([]);const stepX=(w-pad)/(d.length-1);ctx.beginPath();ctx.moveTo(pad,y0);d.forEach((v,i)=>{ctx.lineTo(pad+(i*stepX),getY(v))});ctx.lineTo(w,y0);ctx.fillStyle=fillGrd;ctx.fill();ctx.beginPath();d.forEach((v,i)=>{let x=pad+(i*stepX);let y=getY(v);if(i==0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.lineWidth=2;ctx.strokeStyle=strokeGrd;ctx.shadowBlur=10;ctx.shadowColor="rgba(255,255,255,0.2)";ctx.stroke();ctx.shadowBlur=0;ctx.fillStyle='#666';ctx.textAlign='center';ctx.textBaseline="top";for(let i=d.length-1;i>=0;i-=20){let x=pad+(i*stepX);let date=new Date((ts-((d.length-1-i)*180))*1000);let hrs=date.getHours();ctx.beginPath();ctx.strokeStyle='rgba(255,255,255,0.05)';ctx.moveTo(x,0);ctx.lineTo(x,h-bPad);ctx.stroke();ctx.beginPath();ctx.strokeStyle='#666';ctx.moveTo(x,h-bPad);ctx.lineTo(x,h-bPad+5);ctx.stroke();if(hrs%2===0||i===d.length-1){let label=hrs.toString().padStart(2,'0')+":"+date.getMinutes().toString().padStart(2,'0');if(hrs===0){label=date.getDate().toString().padStart(2,'0')+"."+(date.getMonth()+1).toString().padStart(2,'0')+".";ctx.fillStyle='#ccc'}else{ctx.fillStyle='#666'}ctx.fillText(label,x,h-bPad+5)}}}
    function drawBarChart(din, dout) {
        if(!din || !dout) return;
        const c=ctx.canvas; c.width=c.clientWidth; c.height=220; const w=c.width, h=c.height, pad=60, bPad=30, tPad=30; ctx.clearRect(0,0,w,h);
        let maxVal = 0; for(let i=0; i<7; i++) { if(din[i]>maxVal) maxVal=din[i]; if(dout[i]>maxVal) maxVal=dout[i]; }
        if(maxVal<1) maxVal=1; else maxVal*=1.1;
        const drawH = h - bPad - tPad; const getY = (v) => tPad + drawH - (v/maxVal * drawH);
        ctx.strokeStyle = '#333'; ctx.lineWidth=1; ctx.beginPath(); ctx.moveTo(pad, getY(0)); ctx.lineTo(w, getY(0)); ctx.stroke(); ctx.beginPath(); ctx.moveTo(pad, getY(maxVal)); ctx.lineTo(w, getY(maxVal)); ctx.stroke();
        ctx.fillStyle = '#aaa'; ctx.textAlign='right'; ctx.font='10px sans-serif'; ctx.fillText(maxVal.toFixed(1)+" kWh", pad-5, getY(maxVal)+3); ctx.fillText("0", pad-5, getY(0)+3);
        const barW = (w-pad)/7; const barGap = 4; const subBarW = (barW - barGap*2)/2;
        for(let i=0; i<7; i++) {
            let xBase = pad + ((6-i) * barW) + barGap; let hIn = (h-bPad) - getY(din[i]); ctx.fillStyle = '#10b981'; ctx.fillRect(xBase, getY(din[i]), subBarW, hIn); if(din[i]>0.1) { ctx.fillStyle='#fff'; ctx.textAlign='center'; ctx.fillText(din[i].toFixed(1), xBase+subBarW/2, getY(din[i])-2); }
            let hOut = (h-bPad) - getY(dout[i]); ctx.fillStyle = '#f59e0b'; ctx.fillRect(xBase + subBarW, getY(dout[i]), subBarW, hOut); if(dout[i]>0.1) { ctx.fillStyle='#fff'; ctx.textAlign='center'; ctx.fillText(dout[i].toFixed(1), xBase+subBarW+subBarW/2, getY(dout[i])-2); }
            ctx.fillStyle = '#999'; ctx.textAlign='center'; let label = (i==0) ? T('today') : "-"+i; ctx.fillText(label, xBase + subBarW, h-5);
        }
    }
    function update(){
      fetch('/data').then(r=>r.json()).then(d=>{
        if(d.config && d.config.cnt) svcSyncOptions(d.config.cnt);
        beautifyServicePanel();
        document.getElementById('t-soc').innerText=d.victron.avg_soc.toFixed(1)+'%';
        document.getElementById('t-pwr').innerText=d.victron.total_power.toFixed(0)+' W';
        document.getElementById('t-cur').innerText=d.victron.total_amps.toFixed(1)+' A';
        document.getElementById('t-vol').innerText=d.victron.voltage.toFixed(2)+' V';
        document.getElementById('t-cap').innerText=d.victron.rem_cap.toFixed(0)+'/'+d.victron.total_cap.toFixed(0)+' Ah';
        document.getElementById('t-mix').innerHTML=d.victron.avg_soh.toFixed(0)+'% / '+d.victron.avg_temp.toFixed(1)+'&deg;C';
        document.getElementById('t-cnt').innerText=d.victron.active;
        document.getElementById('e-in').innerText=d.energy.in.toFixed(1)+' kWh';
        document.getElementById('e-out').innerText=d.energy.out.toFixed(1)+' kWh';
        const sc=document.getElementById('sysCard'); const ss=document.getElementById('sysState');
        let sKey = 'IDLE'; let borderCol = '#6366f1'; let glowClass = 'glow-idle';
        if(d.victron.total_power>50) { borderCol='#10b981'; glowClass='glow-chg'; ss.className='pill p-green'; sKey='CHG'; }
        else if(d.victron.total_power<-50) { borderCol='#f59e0b'; glowClass='glow-dis'; ss.className='pill p-orange'; sKey='DIS'; }
        else { ss.className='pill p-blue'; }
        ss.innerText = ST_TXT[lang][sKey];
        let th = document.getElementById('i_theme').value;
        if(th != "1" && th != "3" && th != "4" && th != "6" && th != "7") { sc.className = "card draggable " + glowClass; } else { sc.className = "card draggable"; }
        if(th != "1" && th != "3" && th != "4" && th != "6" && th != "7") { let sh = document.getElementById('sysHead'); sh.style.borderTop = "4px solid " + borderCol; }
        if(th == "3") { if(document.body.className !== 'theme-live') document.body.className = 'theme-live'; let soc = d.victron.avg_soc; let col = (soc < 20) ? "#991b1b" : (soc < 50) ? "#b45309" : "#065f46"; document.body.style.background = `linear-gradient(to top, ${col} ${soc}%, #111 ${soc}%)`; } 
        else if(th == "4") { document.body.className = 'theme-cyber'; } else if(th == "6") { document.body.className = 'theme-custom'; document.getElementById('editBtn').style.display='inline-block'; enableDrag(); } else { document.body.className = ''; document.getElementById('editBtn').style.display='none'; }
        if(d.can_error) { document.getElementById('canErr').style.display='block'; document.getElementById('canErr').innerText=T('can_error')+': '+d.can_status; } else document.getElementById('canErr').style.display='none';
        if(d.sd_err || (document.getElementById('i_sd').checked && !d.sd_ok)) { let el = document.getElementById('sysErr'); el.style.display='block'; el.innerText = d.sd_err ? T('sd_write_error') : T('no_card'); } else { document.getElementById('sysErr').style.display='none'; }
        const safeEl=document.getElementById('safeErr'); if(d.safe_lock){ safeEl.style.display='block'; safeEl.innerText=T('safe_cutoff')+(d.alarm&&d.alarm!=='OK'?': '+d.alarm:''); } else safeEl.style.display='none';
        document.getElementById('sd_stat').innerText = T('status') + ": " + (d.sd_ok ? T('ok') : T('no_card'));
        document.getElementById('log').innerHTML=d.log;
        
        // --- FIX V130: Disable Checkboxes ---
        if(!d.sd_ok) { 
            document.getElementById('i_sd').disabled = true; document.getElementById('i_spy').disabled = true;
            document.getElementById('i_sd').parentElement.style.opacity = '0.5';
            document.getElementById('i_spy').parentElement.style.opacity = '0.5';
        } else {
            document.getElementById('i_sd').disabled = false; document.getElementById('i_spy').disabled = false;
            document.getElementById('i_sd').parentElement.style.opacity = '1';
            document.getElementById('i_spy').parentElement.style.opacity = '1';
        }
        
        // --- Live display (runtime vs configured limits) ---
        if(d.victron && d.config) {
             let cfg_c = d.config.cfg_chg || 0;
             let cfg_d = d.config.cfg_dis || 0;
             let real_c = d.victron.max_chg;
             let real_d = d.victron.max_dis;
             
             let col_c = (real_c < cfg_c) ? "#f59e0b" : "#10b981"; 
             let col_d = (real_d < cfg_d) ? "#f59e0b" : "#f59e0b"; 

             document.getElementById('v_chg').innerHTML = `<span style="color:${col_c}">${real_c} A</span> <span style="opacity:0.5;font-size:0.7em;">/ ${cfg_c} A</span>`;
             document.getElementById('v_dis').innerHTML = `<span style="color:${col_d}">${real_d} A</span> <span style="opacity:0.5;font-size:0.7em;">/ ${cfg_d} A</span>`;
        }
        if(d.config) {
             document.getElementById('v_t_chg').innerHTML = d.config.t_c_min + "&deg; / " + d.config.t_c_max + "&deg;";
             document.getElementById('v_t_dis').innerHTML = d.config.t_d_min + "&deg; / " + d.config.t_d_max + "&deg;";
        }

        if(!initDone && d.config) {
            document.getElementById('i_cnt').value = d.config.cnt; document.getElementById('i_cells').value = d.config.cells;
            document.getElementById('i_chg').value = d.config.chg; document.getElementById('i_dis').value = d.config.dis;
            document.getElementById('i_cvl').value = d.config.cvl; document.getElementById('chk_exp').checked = d.config.exp;
            if(d.config.exp) document.getElementById('exp_box').style.display='block';
            if(d.config.s_vol) document.getElementById('i_svol').value = d.config.s_vol;
            if(d.config.s_cel) document.getElementById('i_scel').value = d.config.s_cel;
            document.getElementById('i_tcmin').value = d.config.t_c_min;
            document.getElementById('i_tcmax').value = d.config.t_c_max;
            document.getElementById('i_tdmin').value = d.config.t_d_min;
            document.getElementById('i_tdmax').value = d.config.t_d_max;
            document.getElementById('i_tmode').value = d.config.t_mode;
            document.getElementById('i_sdrift').value = d.config.s_drift;
            document.getElementById('i_theme').value = d.config.theme; document.getElementById('i_can_proto').value = d.config.can_proto; 
            document.getElementById('i_ntp').value = d.config.ntp; document.getElementById('i_tz').value = d.config.tz;
            document.getElementById('i_sd').checked = d.config.sd; document.getElementById('i_debug').checked = d.config.debug; 
            document.getElementById('i_spy').checked = d.config.spy; document.getElementById('i_vic_en').checked = d.config.vic_en;
            document.getElementById('i_easy').checked = !!d.config.easy; document.getElementById('i_soc_mode').value = (d.config.soc_mode!==undefined?String(d.config.soc_mode):'2'); document.getElementById('i_maint_en').checked = !!d.config.maint_en; document.getElementById('i_maint_v').value = (d.config.maint_v!==undefined?d.config.maint_v:54.6); document.getElementById('i_ab_en').checked = (d.config.ab_en!==undefined?!!d.config.ab_en:true);
            document.getElementById('i_mq_en').checked = d.config.mq_en; document.getElementById('i_mq_full').checked = d.config.mq_full; 
            document.getElementById('i_ha_en').checked = d.config.ha_en; document.getElementById('i_mq_ip').value = d.config.mq_ip;
            document.getElementById('i_mq_pt').value = d.config.mq_pt; document.getElementById('i_mq_us').value = d.config.mq_us;
            if(d.config.sd) loadFiles(); 
            initDone = true;
        }
        if(d.config && d.config.setup_done===false && !setupWizardShown) {
            setupWizardShown = true;
            showSetupWizardDialog();
        }
        const con = document.getElementById('bmsCon');
        d.bms.forEach((b,i)=>{
           let el = document.getElementById('bms-'+i);
           let cg=''; b.cells.forEach(v=>{ cg+=`<div class="c-box ${v==b.min_cell?'c-min':''} ${v==b.max_cell?'c-max':''}">${v.toFixed(3)}</div>`; });
           let tg=''; b.temps.forEach(t=>{ tg+=`<div class="c-box"><div class="c-lbl">${t.lbl}</div>${t.val.toFixed(1)}&deg;</div>`; });
           let diff = (b.max_cell - b.min_cell).toFixed(3); let pwr = Math.round(b.pack_v * b.current);
           let bKey='IDLE', pc='p-blue'; let bGlow = 'glow-idle'; let bCol = 'transparent';
           if(b.current>0.5){bKey='CHG'; pc='p-green'; bGlow='glow-chg'; bCol='#10b981';} 
           else if(b.current<-0.5){bKey='DIS'; pc='p-orange'; bGlow='glow-dis'; bCol='#f59e0b';}
           if(!b.online){bKey='OFF'; pc='p-red'; bGlow='glow-err'; bCol='#ef4444';}
           let stTxt = ST_TXT[lang][bKey];
           let headHTML = '';
           if(view==='list') {
               headHTML = `<div class="list-head-grid bms-border" style="border-left: 5px solid ${bCol};" onclick="toggleDetails(this)"><div><span style="font-weight:bold; opacity:0.7;">#${i}</span></div><div style="font-size:1.2rem; font-weight:bold; color:${bCol}">${b.soc}%</div><div class="stat-row"><span>${b.pack_v.toFixed(2)}V</span> <span>${b.current.toFixed(1)}A</span> <span>${pwr}W</span><span style="opacity:0.7">SOH:${b.soh}%</span> <span style="opacity:0.7">T:${b.avg_temp.toFixed(1)}&deg;</span><span style="opacity:0.7">D:${diff}</span></div><div><span class="pill ${pc}">${stTxt}</span></div></div>`;
           } else {
               headHTML = `<div style="font-weight:bold; display:flex; justify-content:space-between;"><span>${T('bms')} #${i}</span> <span class="pill ${pc}">${stTxt}</span></div><div style="margin-top:5px; font-size:0.9rem;">${b.soc}% | ${b.pack_v.toFixed(2)}V | ${b.current.toFixed(1)}A | ${pwr}W</div>`;
           }
           let bodyHTML = `<div class="b-det" style="${view==='list'?'display:none; padding:10px; border-top:1px solid rgba(255,255,255,0.1);':''}"> <div class="sub-grid" style="margin-bottom:10px;"> <div class="box"><span>${T('diff')}:</span> ${diff} V</div> <div class="box"><span>${T('soh')}:</span> ${b.soh}%</div> </div> <details><summary>${T('cells')}</summary><div class="c-grid">${cg}</div></details> <div class="c-grid">${tg}</div> </div>`;
           if(!el) {
               let cls = view==='list' ? 'list-item' : 'card draggable '+bGlow; let style = `margin-bottom:10px; ${view!=='list'?'padding:10px; border-top: 5px solid '+bCol+'; border-right: 5px solid '+bCol:''}`;
               let h = `<div id="bms-${i}" class="${cls}" style="${style}" draggable="false"><div class="drag-handle">::: Drag BMS #${i} :::</div><div id="bms-h-${i}">${headHTML}</div> <div id="bms-b-${i}">${bodyHTML}</div></div>`;
               con.insertAdjacentHTML('beforeend', h);
           } else {
               if(view!=='list' && th != "1" && th != "3" && th != "4" && th != "7") { el.className = 'card draggable ' + bGlow; el.style.borderTop = "5px solid " + bCol; el.style.borderRight = "5px solid " + bCol; el.style.borderLeft = "none"; } else { el.className = (view==='list' ? 'list-item' : 'card draggable'); el.style.borderTop = "none"; el.style.borderRight = "none"; }
               el.style.padding = view==='list' ? '0' : '10px';
               document.getElementById('bms-h-'+i).innerHTML = headHTML;
               let bEl = document.getElementById('bms-b-'+i);
               let dtl = bEl.querySelector('details'); let isOpen = dtl ? dtl.hasAttribute('open') : false;
               let isVis = false; if(view==='list') { let detDiv = bEl.querySelector('.b-det'); if(detDiv && detDiv.style.display!='none') isVis=true; }
               bEl.innerHTML = bodyHTML;
               if(isOpen && bEl.querySelector('details')) bEl.querySelector('details').setAttribute('open','');
               if(view==='list' && isVis && bEl.querySelector('.b-det')) bEl.querySelector('.b-det').style.display='block';
           }
        });
        if(chartMode === 'power') { if(d.history && d.ts) drawChart(d.history, d.ts); } else { if(d.days_in && d.days_out) drawBarChart(d.days_in, d.days_out); }
      });
    }
    function toggleView(){ view=(view==='grid'?'list':'grid'); document.getElementById('btnView').innerText=(view==='grid'?'LIST':'GRID'); const m=document.getElementById('mainGrid'); if(view==='list'){ m.style.display='flex'; m.style.flexDirection='column'; } else { m.style.display='grid'; } document.getElementById('bmsCon').innerHTML=''; update(); }
    setInterval(update, 2000); updateText(); updateClock();
  </script>
</body>
</html>
)rawliteral";
  String fwUi = "Topband V" + String(FW_VERSION);
  h.replace("Topband V142 Master", fwUi + " Master");
  h.replace("Topband V142", fwUi);
  
  server.send(200, "text/html", h);
}

#endif  // END OF MODE_SMART_WIFI

// --- SETUP (WIFI FIRST LOGIC - V142) ---
void setup() {
#if HAS_M5_UI
  initM5Ui();
#else
  Serial.begin(115200);
#endif
  Serial.printf("\n=== Topband Gateway V%s ===\n", FW_VERSION);

  #if defined(BOARD_LILYGO)
    pinMode(PIN_5V_EN, OUTPUT); digitalWrite(PIN_5V_EN, HIGH);
    pinMode(PIN_RS485_SE, OUTPUT); digitalWrite(PIN_RS485_SE, HIGH);
    pinMode(PIN_RS485_RE, OUTPUT); digitalWrite(PIN_RS485_RE, HIGH);
    pinMode(PIN_CAN_SE, OUTPUT); digitalWrite(PIN_CAN_SE, LOW);
    delay(50); // boost + RS485 rail settle
  #elif defined(BOARD_WAVESHARE)
    pinMode(PIN_RS485_DIR, OUTPUT); digitalWrite(PIN_RS485_DIR, LOW);
  #endif

#if HAS_NEOPIXEL
  pixels.begin(); pixels.setBrightness(30);
#endif
  setLed(0,0,255); // BLAU = Start
  delay(500);

  #ifdef MODE_SMART_WIFI
    // 1. ZUERST WIFI MANAGER
    WiFiManager wm;
    wm.setAPCallback(configModeCallback); 
    wm.setConnectTimeout(30); 
    wm.setConfigPortalTimeout(180); 

    // HOSTNAME VORHER SETZEN (FIX V137)
    WiFi.setHostname(HOSTNAME); 

    bool fixed_wifi_ok = false;
    if (String(FIXED_SSID) != "") {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.persistent(false);
        WiFi.disconnect(true, true);
        delay(150);
        WiFi.begin(FIXED_SSID, FIXED_PASS);
        int tr = 0;
        while (WiFi.status() != WL_CONNECTED && tr < 80) {
            delay(500);
            tr++;
        }
        fixed_wifi_ok = (WiFi.status() == WL_CONNECTED);
    }

    setLed(150, 0, 150); // LILA = WiFi Phase

    if (!fixed_wifi_ok && !wm.autoConnect("Victron-Gateway-Setup")) {
        Serial.println("AP Timeout");
        setLed(255, 0, 0); // Rot = Timeout
    } else {
        if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
        configTime(g_timezone_offset * 3600, 0, g_ntp_server.c_str());
        setLed(0, 255, 255); // Tuerkis = WiFi OK
    }

    // 2. JETZT ERST SETTINGS LADEN
    preferences.begin("gateway", false);
    g_bms_count = preferences.getInt("cnt", 2); 
    g_force_cell_count = preferences.getInt("cells", 0);
    g_charge_amps = preferences.getFloat("chg", 30.0);
    g_discharge_amps = preferences.getFloat("dis", 30.0);
    g_expert_mode = preferences.getBool("exp", true);
    if(g_expert_mode) g_cvl_voltage = preferences.getFloat("cvl", 52.5); else g_cvl_voltage = 52.5;
    g_safe_volt = preferences.getFloat("s_vol", 53.25);
    g_safe_cell = preferences.getFloat("s_cel", 3.55);
    // Default charge min temp is 5.0 C
    g_tc_min = preferences.getFloat("t_c_min", 5.0);
    g_tc_max = preferences.getFloat("t_c_max", 50.0);
    g_td_min = preferences.getFloat("t_d_min", -20.0);
    g_td_max = preferences.getFloat("t_d_max", 60.0);
    g_temp_mode = preferences.getInt("t_mode", 0);
    g_safe_drift = preferences.getFloat("s_drift", 0.20);
    // Config Flags
    g_sd_enable = preferences.getBool("sd_en", false); 
    g_serial_debug = preferences.getBool("debug", false); 
    g_sd_spy = preferences.getBool("spy", false); 
    g_mqtt_enable = preferences.getBool("mq_en", false);
    g_mqtt_full = preferences.getBool("mq_full", false); 
    g_ha_enable = preferences.getBool("ha_en", false); 
    g_theme_id = preferences.getInt("theme", 0);
    g_can_protocol = preferences.getInt("can_proto", 0); 
    g_victron_enable = preferences.getBool("vic_en", true); 
    g_soc_source_mode = preferences.getInt("soc_mode", 2);
    if (g_soc_source_mode < 0 || g_soc_source_mode > 2) g_soc_source_mode = 2;
    g_maint_charge_mode = preferences.getBool("maint_en", false);
    g_maint_target_v = preferences.getFloat("maint_v", 51.00f);
    if (g_maint_target_v < 40.0f) g_maint_target_v = 40.0f;
    if (g_maint_target_v > 65.0f) g_maint_target_v = 65.0f;
    g_auto_balance_enable = preferences.getBool("ab_en", true);
    g_auto_balance_last_ts = preferences.getULong("ab_last", 0);
    g_easy_mode = preferences.getBool("easy", false);
    g_setup_done = preferences.getBool("setupd", true);
    if (!g_expert_mode) {
    g_easy_mode = false;
    g_charge_amps = 30.0f;
    g_discharge_amps = 30.0f;
    g_cvl_voltage = 52.5f;
    g_safe_volt = 53.25f;
    g_safe_cell = 3.55f;
    g_safe_drift = 0.20f;
    g_tc_min = 5.0f;
    g_tc_max = 50.0f;
    g_td_min = -20.0f;
    g_td_max = 60.0f;
    g_maint_target_v = 51.0f;
  }
    g_mqtt_server = preferences.getString("mq_ip", ""); g_mqtt_port = preferences.getInt("mq_pt", 1883);
    g_mqtt_user = preferences.getString("mq_us", "");
    g_mqtt_pass = preferences.getString("mq_pw", "");
    g_ntp_server = preferences.getString("ntp", "pool.ntp.org"); g_timezone_offset = preferences.getInt("tz", 1);
    preferences.end();
    
    // 3. Init Services
    addToLog("System Start. Waiting for BMS...", false); 
    loadHistory(); 
    initSD();
    if (g_mqtt_enable && g_mqtt_server != "") { mqtt.setServer(g_mqtt_server.c_str(), g_mqtt_port); mqtt.setBufferSize(4096); }

    // 4. Server Routen
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/sim", handleSim); server.on("/save", handleSave);
    server.on("/svc/bms", HTTP_GET, handleServiceBmsDiag);
    server.on("/svc/bms/time", HTTP_GET, handleServiceBmsTimeGet);
    server.on("/svc/bms/time/set", HTTP_GET, handleServiceBmsTimeSet);
    server.on("/svc/autocfg/apply", HTTP_GET, handleServiceAutoCfgApply);
    server.on("/ha/discovery/send", HTTP_GET, handleHaDiscoverySend);
    server.on("/ha/discovery/clear", HTTP_GET, handleHaDiscoveryClear);
    server.on("/sd/download", HTTP_GET, handleSDDownload); 
    server.on("/sd/spy", HTTP_GET, handleSpyDownload); 
    server.on("/sd/clear", HTTP_GET, handleSDDelete); 
    server.on("/sd/list", HTTP_GET, handleSDList); 
    server.begin();
    setLed(0,0,0); // LED off, enter loop
  #else
    // CABLE MODE
    g_bms_count = 2; 
    g_can_protocol = 0;
  #endif

  RS485.setRxBufferSize(1024);
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  initCanBus();

  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t wdt_cfg = { .timeout_ms = WDT_TIMEOUT * 1000, .idle_core_mask = (1 << 0), .trigger_panic = true };
  esp_task_wdt_init(&wdt_cfg);
  #else
  esp_task_wdt_init(WDT_TIMEOUT, true);
  #endif
  esp_task_wdt_add(NULL);
#if HAS_M5_UI
  drawM5Ui();
#endif
}

void loop() {
  esp_task_wdt_reset(); 
  unsigned long now = millis();

  static unsigned long last_led = 0;
  if(now - last_led > 50) { last_led = now; handleLedStatus(); }

  #ifdef MODE_SMART_WIFI
    server.handleClient();
    unsigned long now_hist = millis();
    if (now_hist - last_hist_time > HISTORY_INTERVAL) { last_hist_time = now_hist; updateHistory(); }
    calculateEnergy();
    if (g_sd_enable && (now_hist - last_sd_time > SD_LOG_INTERVAL)) { last_sd_time = now_hist; writeLogToSD(); }
    mqttReconnect();
    if (g_mqtt_enable && WiFi.status() == WL_CONNECTED && mqtt.connected()) { mqtt.loop(); if (now_hist - last_mqtt_time > MQTT_INTERVAL) { last_mqtt_time = now_hist; sendMqttData(); } }
    if (!g_rs485_service_busy && !simulation_active && (now - last_alarm_poll_time > ALARM_POLL_INTERVAL)) {
      last_alarm_poll_time = now;
      pollBmsAlarmStatus(next_alarm_poll_bms);
      next_alarm_poll_bms++;
      if (next_alarm_poll_bms >= g_bms_count) next_alarm_poll_bms = 0;
    }
    if (!g_rs485_service_busy && !simulation_active && (now - last_sysparam_poll_time > SYSPARAM_POLL_INTERVAL)) {
      last_sysparam_poll_time = now;
      pollBmsSystemParameter(next_sysparam_poll_bms);
      next_sysparam_poll_bms++;
      if (next_sysparam_poll_bms >= g_bms_count) next_sysparam_poll_bms = 0;
    }
  #endif

#if HAS_M5_UI
  handleM5Buttons();
  if (now - last_ui_time > UI_INTERVAL) {
    last_ui_time = now;
    drawM5Ui();
  }
#endif

  if (!g_rs485_service_busy && !simulation_active && (now - last_poll_time > POLL_INTERVAL)) {
    last_poll_time = now;
    for (int i = 0; i < g_bms_count; i++) {
      esp_task_wdt_reset();
      String resp = "";
      if (rs485ExchangeFrame(String(POLL_CMDS[i]), resp, RS485_TIMEOUT)) {
        // strip SOI '~' for legacy parser which expects hex payload without tilde
        int soi = resp.indexOf('~');
        if (soi >= 0) resp = resp.substring(soi + 1);
        int eoi = resp.indexOf('\r');
        if (eoi < 0) eoi = resp.indexOf('\n');
        if (eoi >= 0) resp = resp.substring(0, eoi);
        if (resp.length() > 0) parseTopband(resp, i);
      } else {
#ifdef MODE_SMART_WIFI
        addToLog("Timeout BMS " + String(i), true);
#endif
      }
      delay(BUS_GUARD_TIME);
    }
  }
  
  if (now - last_can_time > 500) {
    last_can_time = now;
    calculateVictronData();
    if (g_victron_enable) { sendVictronCAN(); }
  }
}




