#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <driver/twai.h>
#include <esp_task_wdt.h>
#include <time.h>

// ===================================
// CONFIG (V117 - STABLE CORE / WDT FIX)
// ===================================
#define BOARD_TYPE 2  // 2 = Waveshare S3
const char* FIXED_SSID = ""; // LEER LASSEN für WiFiManager
const char* FIXED_PASS = ""; 
const char *HOSTNAME = "victron-gateway";

// --- SAFETY LIMITS ---
#define SAFETY_VOLT_MAX   56.5  
#define SAFETY_TEMP_MIN   0.0   
#define SAFETY_TEMP_MAX   50.0  
#define STD_CHARGE_VOLT   52.5  

#if BOARD_TYPE == 1 // LilyGo
#define RS485_TX_PIN 22
#define RS485_RX_PIN 21
#define PIN_PWR_BOOST 16
#define PIN_485_EN 19
#define PIN_485_CB 17
#define CAN_TX_PIN 27
#define CAN_RX_PIN 26
#define LED_PIN 4
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS 13
#elif BOARD_TYPE == 2 // Waveshare S3
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18
#define PIN_RS485_DIR 21
#define CAN_TX_PIN 15
#define CAN_RX_PIN 16
#define LED_PIN 38
#define SD_MISO -1
#define SD_MOSI -1
#define SD_SCLK -1
#define SD_CS -1
#endif

#define NUM_LEDS 1
#define MAX_BMS 16
#define RS485_BAUD 9600
#define POLL_INTERVAL 3000
#define RS485_TIMEOUT 600
#define BUS_GUARD_TIME 200
#define WDT_TIMEOUT 10 // Watchdog Timeout in Sekunden

#define HISTORY_LEN 960 
#define HISTORY_INTERVAL 180000 
#define SD_LOG_INTERVAL 60000
#define MQTT_INTERVAL 5000

const char *POLL_CMDS[16] = {
    "~21004642E00200FD36\r", "~21014642E00201FD34\r", "~21024642E00202FD32\r",
    "~21034642E00203FD30\r", "~21044642E00204FD2E\r", "~21054642E00205FD2C\r",
    "~21064642E00206FD2A\r", "~21074642E00207FD28\r", "~21084642E00208FD26\r",
    "~21094642E00209FD24\r", "~210A4642E0020AFD14\r", "~210B4642E0020BFD12\r",
    "~210C4642E0020CFD10\r", "~210D4642E0020DFD0E\r", "~210E4642E0020EFD0C\r",
    "~210F4642E0020FFD0A\r"};

WebServer server(80);
Preferences preferences;
Preferences histStore;
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial RS485(1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

int g_bms_count = 2;
float g_charge_amps = 20.0;
float g_discharge_amps = 30.0;
int g_force_cell_count = 0;
float g_cvl_voltage = STD_CHARGE_VOLT;
bool g_expert_mode = false;
bool g_sd_enable = false;
bool g_victron_enable = true; 
int g_theme_id = 0; 
String g_mqtt_server = "";
int g_mqtt_port = 1883;
String g_mqtt_user = "";
String g_mqtt_pass = "";
bool g_mqtt_enable = false;
String g_ntp_server = "pool.ntp.org";
int g_timezone_offset = 1;

bool simulation_active = false;
unsigned long last_poll_time = 0;
unsigned long last_can_time = 0;
unsigned long last_mqtt_time = 0;
unsigned long last_sd_time = 0;
unsigned long last_hist_time = 0;
unsigned long last_flash_save = 0;

String debug_log = "";
String debug_can_status = "Init...";
bool can_error_flag = false;
bool sd_ok = false;
bool sd_error_flag = false;
int sys_error_state = 0; 

int16_t powerHistory[HISTORY_LEN];
int historyIdx = 0; 
float daily_in[7];
float daily_out[7];
unsigned long last_energy_calc = 0;
int current_day = -1;
unsigned long stat_rx_count = 0;
unsigned long stat_tx_count = 0;

struct BMSData {
  bool valid; float voltage; float current; int soc; int soh; float rem_ah; float full_ah; float cells[32];
  int cell_count; float temps[8]; int temp_count; float minCellV; float maxCellV; float avgCellV;
  int minCellIdx; int maxCellIdx; float maxTemp; float avgTemp; unsigned long last_seen;
};
BMSData bms[MAX_BMS];

struct VictronType {
  float totalCurrent; float avgVoltage; float avgSOC; float avgSOH; float maxChargeCurrent; float maxDischargeCurrent;
  float avgTemp; float totalCapacity; float remainCapacity; float totalPower; int activePacks;
} victronData;

void setLed(int r, int g, int b) { pixels.setPixelColor(0, pixels.Color(r, g, b)); pixels.show(); }
String getTimeStr() {
  struct tm t; if (!getLocalTime(&t)) return String(millis() / 1000) + "s";
  char b[20]; strftime(b, sizeof(b), "%H:%M:%S", &t); return String(b);
}
void addToLog(String msg, bool error) {
  String clr = error ? "log-err" : "log-ok";
  if (debug_log.length() > 6000) debug_log = debug_log.substring(0, 5000);
  debug_log = "<div class='" + clr + "'>[" + getTimeStr() + "] " + msg + "</div>" + debug_log;
  Serial.println(msg);
}
uint16_t get_u16(const uint8_t *b, int o) { return (b[o] << 8) | b[o + 1]; }
int16_t get_s16(const uint8_t *b, int o) { uint16_t v = get_u16(b, o); return (v > 32767) ? (v - 65536) : v; }
uint8_t parse_hex(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return 0xFF; }

void parseTopband(String raw, int addr) {
  if (raw.length() < 20) { addToLog("BMS " + String(addr) + ": Short", true); return; }
  stat_rx_count++; uint8_t b[512]; int blen = 0;
  for (int i = 0; i < raw.length() - 1; i += 2) { if (blen >= 511) break; b[blen++] = (parse_hex(raw[i]) << 4) | parse_hex(raw[i + 1]); }
  int idx = -1; for (int i = 0; i < blen - 1; i++) { if (b[i] == 0xD0 && b[i + 1] == 0x7C) { idx = i; break; } }
  if (idx == -1) return;
  int p = idx + 2; p++; int cells = b[p++];
  if (g_force_cell_count > 0) cells = g_force_cell_count; if (cells > 32) cells = 32;
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
    if (volt > 100 || abs(cur) > 500) { addToLog("BMS " + String(addr) + " Bad Main", true); return; }
    bms[addr].current = cur; bms[addr].voltage = volt; bms[addr].rem_ah = rem; bms[addr].full_ah = full;
    bms[addr].soc = (full > 0) ? (int)((rem / full) * 100.0) : 0; if (bms[addr].soc > 100) bms[addr].soc = 100;
    if (p + 3 < blen) { p += 2; uint16_t s = get_u16(b, p); if(s > 100) s = 100; bms[addr].soh = s; } else { bms[addr].soh = 100; }
  }
  bms[addr].valid = true; bms[addr].last_seen = millis();
}

void calculateVictronData() {
  float sumI = 0, sumV = 0, sumSOC = 0, sumSOH = 0, sumT = 0, sumCap = 0, sumRem = 0;
  int count = 0; sys_error_state = 0;
  for (int i = 0; i < g_bms_count; i++) {
    bool on = bms[i].valid && (millis() - bms[i].last_seen < 60000);
    if (simulation_active) on = true;
    if (on) {
      if(bms[i].voltage > SAFETY_VOLT_MAX) sys_error_state = 1; 
      if(bms[i].avgTemp < SAFETY_TEMP_MIN || bms[i].avgTemp > SAFETY_TEMP_MAX) { if(sys_error_state == 0) sys_error_state = 2; }
      sumI += bms[i].current; sumV += bms[i].voltage; sumSOC += bms[i].soc; sumSOH += bms[i].soh; sumT += bms[i].avgTemp; sumCap += bms[i].full_ah; sumRem += bms[i].rem_ah; count++;
    }
  }
  if (count > 0) {
    victronData.activePacks = count; victronData.totalCurrent = sumI; victronData.avgVoltage = sumV / count;
    victronData.totalPower = sumI * victronData.avgVoltage; victronData.avgSOC = sumSOC / count;
    victronData.avgSOH = sumSOH / count; victronData.avgTemp = sumT / count; victronData.totalCapacity = sumCap; victronData.remainCapacity = sumRem;
    if(sys_error_state > 0) { victronData.maxChargeCurrent = 0; victronData.maxDischargeCurrent = 0; } 
    else {
        victronData.maxChargeCurrent = count * g_charge_amps; victronData.maxDischargeCurrent = count * g_discharge_amps;
        if (victronData.avgSOC >= 99) victronData.maxChargeCurrent = count * 2.0;
        if (victronData.avgSOC == 100) victronData.maxChargeCurrent = 0.0;
    }
  } else { victronData.activePacks = 0; victronData.totalCurrent = 0; victronData.totalPower = 0; victronData.maxChargeCurrent = 0; victronData.maxDischargeCurrent = 0; }
}

void loadHistory() {
  histStore.begin("h", true);
  for (int i = 0; i < 7; i++) { daily_in[i] = histStore.getFloat(("i" + String(i)).c_str(), 0); daily_out[i] = histStore.getFloat(("o" + String(i)).c_str(), 0); }
  size_t storedLen = histStore.getBytesLength("hdat");
  if(histStore.isKey("hidx") && storedLen == sizeof(powerHistory)) {
      historyIdx = histStore.getInt("hidx", 0); histStore.getBytes("hdat", powerHistory, sizeof(powerHistory));
  } else { for(int i=0; i<HISTORY_LEN; i++) powerHistory[i] = 0; }
  histStore.end();
}
void saveHistory(bool includeGraph) {
  histStore.begin("h", false);
  for (int i = 0; i < 7; i++) { histStore.putFloat(("i" + String(i)).c_str(), daily_in[i]); histStore.putFloat(("o" + String(i)).c_str(), daily_out[i]); }
  if(includeGraph) { histStore.putInt("hidx", historyIdx); histStore.putBytes("hdat", powerHistory, sizeof(powerHistory)); }
  histStore.end(); addToLog("Stats Saved", false);
}
void shiftHistory() {
  for (int i = 6; i > 0; i--) { daily_in[i] = daily_in[i - 1]; daily_out[i] = daily_out[i - 1]; }
  daily_in[0] = 0; daily_out[0] = 0; saveHistory(false);
}
void calculateEnergy() {
  unsigned long now = millis(); if (last_energy_calc == 0) { last_energy_calc = now; return; }
  double h = (now - last_energy_calc) / 3600000.0; last_energy_calc = now;
  double kw = victronData.totalPower / 1000.0;
  if (kw > 0) daily_in[0] += (kw * h); else daily_out[0] += (fabs(kw) * h);
  struct tm t;
  if (getLocalTime(&t)) {
    if (current_day != -1 && current_day != t.tm_mday) shiftHistory();
    current_day = t.tm_mday;
    if (t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec < 5) { if(now - last_flash_save > 60000) { saveHistory(false); last_flash_save = now; } }
  }
}
void updateHistory() { powerHistory[historyIdx] = (int16_t)victronData.totalPower; historyIdx++; if (historyIdx >= HISTORY_LEN) historyIdx = 0; }
void initSD() {
  if (!g_sd_enable || BOARD_TYPE != 1) return;
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) { sd_ok = true; sd_error_flag = false; } else { sd_ok = false; sd_error_flag = true; addToLog("SD Init Fail", true); }
}
void writeLogToSD() {
  if (!g_sd_enable || !sd_ok) return;
  File f = SD.open("/log.csv", FILE_APPEND);
  if (f) { f.printf("%s;%.1f;%.0f;%.1f\n", getTimeStr().c_str(), victronData.avgSOC, victronData.totalPower, victronData.totalCurrent); f.close(); } else { sd_error_flag = true; }
}
void handleSDDownload() { if (!sd_ok) { server.send(404, "text/plain", "No SD"); return; } File f = SD.open("/log.csv"); if (f) { server.streamFile(f, "text/csv"); f.close(); } else server.send(404, "text/plain", "No Log"); }
void handleSDClear() { if (sd_ok && SD.exists("/log.csv")) { SD.remove("/log.csv"); server.send(200, "text/plain", "Deleted"); } else server.send(404, "text/plain", "Err"); }
void mqttReconnect() { if (!g_mqtt_enable || g_mqtt_server == "") return; if (!mqtt.connected() && mqtt.connect("VictronGW", g_mqtt_user.c_str(), g_mqtt_pass.c_str())) mqtt.publish("victron/status", "online"); }
void sendMqttData() { if (g_mqtt_enable && mqtt.connected()) { char j[128]; snprintf(j, sizeof(j), "{\"soc\":%.1f,\"p\":%.0f}", victronData.avgSOC, victronData.totalPower); mqtt.publish("victron/data", j); } }

void sendCanFrame(uint32_t id, uint8_t *data) {
  twai_message_t m; m.identifier = id; m.extd = 0; m.data_length_code = 8; memcpy(m.data, data, 8);
  if (twai_transmit(&m, pdMS_TO_TICKS(5)) == ESP_OK) { setLed(0, 5, 0); can_error_flag = false; debug_can_status = "CAN OK"; } 
  else { setLed(50, 0, 0); can_error_flag = true; debug_can_status = "TX ERR"; }
  stat_tx_count++;
}
void sendVictronCAN() {
  uint8_t d[8] = {0};
  float cvl = g_expert_mode ? g_cvl_voltage : STD_CHARGE_VOLT;
  int cv = (int)(cvl * 10), ccl = (int)(victronData.maxChargeCurrent * 10), dcl = (int)(victronData.maxDischargeCurrent * 10);
  d[0] = cv & 0xFF; d[1] = cv >> 8; d[2] = ccl & 0xFF; d[3] = ccl >> 8; d[4] = dcl & 0xFF; d[5] = dcl >> 8;
  sendCanFrame(0x351, d); delay(2);
  d[0] = (int)victronData.avgSOC & 0xFF; d[1] = (int)victronData.avgSOC >> 8; d[2] = (int)victronData.avgSOH & 0xFF; d[3] = (int)victronData.avgSOH >> 8;
  int cap = (int)(victronData.totalCapacity * 10); d[4] = cap & 0xFF; d[5] = cap >> 8; d[6]=0; d[7]=0;
  sendCanFrame(0x355, d); delay(2);
  int v = (int)(victronData.avgVoltage * 100), i = (int)(victronData.totalCurrent * 10), t = (int)(victronData.avgTemp * 10);
  d[0] = v & 0xFF; d[1] = v >> 8; d[2] = i & 0xFF; d[3] = i >> 8; d[4] = t & 0xFF; d[5] = t >> 8; d[6]=0; d[7]=0;
  sendCanFrame(0x356, d); delay(2);
  memset(d, 0, 8); if(sys_error_state == 1) d[0] = 0x04; else if(sys_error_state == 2) d[0] = 0x40;
  sendCanFrame(0x35A, d); delay(2);
  char n[] = "LILYGO"; sendCanFrame(0x35E, (uint8_t*)n);
}

void handleData() {
  String j = "{ \"victron\": {";
  j += "\"active\": " + String(victronData.activePacks) + ",";
  j += "\"total_amps\": " + String(victronData.totalCurrent, 1) + ",";
  j += "\"total_power\": " + String(victronData.totalPower, 0) + ",";
  j += "\"avg_soc\": " + String(victronData.avgSOC, 1) + ",";
  j += "\"avg_soh\": " + String(victronData.avgSOH, 1) + ",";
  j += "\"voltage\": " + String(victronData.avgVoltage, 2) + ",";
  j += "\"rem_cap\": " + String(victronData.remainCapacity, 0) + ",";
  j += "\"total_cap\": " + String(victronData.totalCapacity, 0) + ",";
  j += "\"avg_temp\": " + String(victronData.avgTemp, 1);
  j += "}, ";
  
  j += "\"config\": {";
  j += "\"cnt\": " + String(g_bms_count) + ", ";
  j += "\"cells\": " + String(g_force_cell_count) + ", ";
  j += "\"chg\": " + String(g_charge_amps) + ", ";
  j += "\"dis\": " + String(g_discharge_amps) + ", ";
  j += "\"cvl\": " + String(g_cvl_voltage) + ", ";
  j += "\"exp\": " + String(g_expert_mode ? "true" : "false") + ", ";
  j += "\"theme\": " + String(g_theme_id) + ", ";
  j += "\"ntp\": \"" + g_ntp_server + "\", ";
  j += "\"tz\": " + String(g_timezone_offset) + ", ";
  j += "\"sd\": " + String(g_sd_enable ? "true" : "false") + ", ";
  j += "\"vic_en\": " + String(g_victron_enable ? "true" : "false") + ", "; 
  j += "\"mq_en\": " + String(g_mqtt_enable ? "true" : "false") + ", ";
  j += "\"mq_ip\": \"" + g_mqtt_server + "\", ";
  j += "\"mq_pt\": " + String(g_mqtt_port) + ", ";
  j += "\"mq_us\": \"" + g_mqtt_user + "\", ";
  j += "\"dummy\":0 }, ";

  struct tm t; unsigned long ts = 0; if(getLocalTime(&t)) { time_t t_now; time(&t_now); ts = (unsigned long)t_now; }
  j += "\"ts\": " + String(ts) + ", ";
  String l = debug_log; l.replace("\"", "'"); l.replace("\n", "");
  j += "\"log\": \"" + l + "\", "; j += "\"can_status\": \"" + debug_can_status + "\", "; j += "\"can_error\": " + String(can_error_flag ? "true" : "false") + ", ";
  j += "\"sd_ok\": " + String(sd_ok ? "true" : "false") + ", "; j += "\"sd_err\": " + String(sd_error_flag ? "true" : "false") + ", ";
  j += "\"safe_lock\": " + String(sys_error_state > 0 ? "true" : "false") + ", ";
  j += "\"history\": [";
  int startIdx = historyIdx; 
  for (int i = 0; i < HISTORY_LEN; i++) { int idx = (startIdx + i) % HISTORY_LEN; if (i > 0) j += ","; j += String(powerHistory[idx]); }
  j += "], ";
  j += "\"days_in\": ["; for(int i=0; i<7; i++) { if(i>0) j+=","; j+=String(daily_in[i],2); } j += "], ";
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
      j += "], \"cells\":[";
      for (int c = 0; c < bms[i].cell_count; c++) { if (c > 0) j += ","; j += String(bms[i].cells[c], 3); }
      j += "]";
    }
    j += "}";
  }
  j += "] }";
  server.send(200, "application/json", j);
}

void handleSim() { if (server.hasArg("act")) simulation_active = (server.arg("act") == "1"); server.send(200); }

void handleSave() {
  saveHistory(true); 
  delay(100);
  preferences.begin("gateway", false); 
  if (server.hasArg("cnt")) g_bms_count = server.arg("cnt").toInt();
  if (server.hasArg("cells")) g_force_cell_count = server.arg("cells").toInt();
  if (server.hasArg("chg")) g_charge_amps = server.arg("chg").toFloat();
  if (server.hasArg("dis")) g_discharge_amps = server.arg("dis").toFloat();
  g_expert_mode = server.hasArg("exp");
  if(g_expert_mode && server.hasArg("cvl")) g_cvl_voltage = server.arg("cvl").toFloat();
  else g_cvl_voltage = STD_CHARGE_VOLT;
  g_sd_enable = server.hasArg("sd_en");
  g_victron_enable = server.hasArg("vic_en"); // SAVE
  if(server.hasArg("theme")) g_theme_id = server.arg("theme").toInt();
  if (server.hasArg("mq_en")) g_mqtt_enable = true; else g_mqtt_enable = false;
  g_mqtt_server = server.arg("mq_ip"); g_mqtt_port = server.arg("mq_pt").toInt();
  g_mqtt_user = server.arg("mq_us"); g_mqtt_pass = server.arg("mq_pw");
  g_ntp_server = server.arg("ntp_svr"); g_timezone_offset = server.arg("tz_off").toInt();

  preferences.putInt("cnt", g_bms_count); preferences.putInt("cells", g_force_cell_count);
  preferences.putFloat("chg", g_charge_amps); preferences.putFloat("dis", g_discharge_amps);
  preferences.putBool("exp", g_expert_mode); preferences.putFloat("cvl", g_cvl_voltage);
  preferences.putBool("sd_en", g_sd_enable); preferences.putBool("mq_en", g_mqtt_enable);
  preferences.putBool("vic_en", g_victron_enable);
  preferences.putInt("theme", g_theme_id); 
  preferences.putString("mq_ip", g_mqtt_server); preferences.putInt("mq_pt", g_mqtt_port);
  preferences.putString("mq_us", g_mqtt_user); preferences.putString("mq_pw", g_mqtt_pass);
  preferences.putString("ntp", g_ntp_server); preferences.putInt("tz", g_timezone_offset);
  preferences.end(); 
  delay(100);
  server.sendHeader("Location", "/"); server.send(303); delay(500); ESP.restart();
}

void handleRoot() {
  String cssVars;
  if(g_theme_id == 1) { cssVars = ":root { --bg-grad: #111; --card-bg: #222; --border: 1px solid #444; --text: #eee; --text-sec: #aaa; --head-col: #1e3a8a; --head-txt: #fff; --bor-top: 1px solid #444; --bor-right: 1px solid #444; --input-bg: #333; --input-txt: #fff; }"; } 
  else if(g_theme_id == 2) { cssVars = ":root { --bg-grad: #ddd; --card-bg: #fff; --border: 2px solid #000; --text: #000; --text-sec: #333; --head-col: #333; --head-txt: #000; --bor-top: 2px solid #000; --bor-right: 2px solid #000; --input-bg: #fff; --input-txt: #000; }"; } 
  else if(g_theme_id == 3) { cssVars = ":root { --bg-grad: #111; --card-bg: rgba(0,0,0,0.6); --border: 1px solid rgba(255,255,255,0.1); --text: #fff; --text-sec: #ccc; --head-col: transparent; --head-txt: #fff; --bor-top: none; --bor-right: none; --input-bg: rgba(0,0,0,0.5); --input-txt: #fff; }"; } 
  else if(g_theme_id == 4) { cssVars = ":root { --bg-grad: #050505; --card-bg: #0a0a0a; --border: 1px solid #0ff; --text: #0ff; --text-sec: #088; --head-col: transparent; --head-txt: #0ff; --bor-top: 1px solid #0ff; --bor-right: 1px solid #0ff; --input-bg: #000; --input-txt: #0ff; }"; } 
  else if(g_theme_id == 5) { cssVars = ":root { --bg-grad: #2b2b2b; --card-bg: #2b2b2b; --border: none; --text: #ddd; --text-sec: #888; --head-col: transparent; --head-txt: #eee; --bor-top: none; --bor-right: none; --input-bg: #252525; --input-txt: #eee; }"; } 
  else if(g_theme_id == 6) { cssVars = ":root { --bg-grad: #222; --card-bg: #333; --border: 1px solid #555; --text: #fff; --text-sec: #aaa; --head-col: #444; --head-txt: #fff; --bor-top: 1px solid #555; --bor-right: 1px solid #555; --input-bg: #444; --input-txt: #fff; }"; } 
  else { cssVars = ":root { --bg-grad: linear-gradient(135deg, #0f172a 0%, #1e293b 100%); --card-bg: rgba(30, 41, 59, 0.7); --border: 1px solid rgba(255,255,255,0.1); --text: #f8fafc; --text-sec: #94a3b8; --head-col: transparent; --head-txt: linear-gradient(90deg, #3b82f6, #8b5cf6); --bor-top: 1px solid rgba(255,255,255,0.4); --bor-right: 1px solid rgba(255,255,255,0.4); --input-bg: rgba(255,255,255,0.1); --input-txt: #fff; }"; }

  String h = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
 <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
 <title>Topband V117</title>
 <style>
  )rawliteral" + cssVars + R"rawliteral(
  body { font-family: 'Segoe UI', sans-serif; background: var(--bg-grad); color: var(--text); margin:0; padding:10px; min-height:100vh; transition: background 0.5s; }
  
  input, select { padding:8px; border-radius:4px; border:none; width:100%; box-sizing: border-box; margin-bottom: 5px; background: var(--input-bg); color: var(--input-txt); }
  select option { background-color: #222; color: #fff; } 
  .check-lbl { display: flex; align-items: center; gap: 10px; width: fit-content; cursor: pointer; padding: 5px; border-radius: 4px; }
  .check-lbl:hover { background: rgba(255,255,255,0.05); }
  
  /* CUSTOM DASH */
  body.theme-custom .grid { display: block; position: relative; }
  body.theme-custom .card { position: relative; margin-bottom: 10px; resize: both; overflow: auto; min-width: 300px; min-height: 100px; }
  
  .grid { display: grid; gap: 15px; grid-template-columns: repeat(auto-fit, minmax(340px, 1fr)); }
  .card { background: var(--card-bg); border: var(--border); border-top: var(--bor-top); border-right: var(--bor-right); border-radius: 12px; padding: 15px; backdrop-filter: blur(15px); transition: all 0.3s; box-shadow: 0 4px 6px rgba(0,0,0,0.2); }
  .pill { padding: 4px 8px; border-radius: 12px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; }
  .p-blue { background: rgba(59, 130, 246, 0.2); color: #60a5fa; }
  .p-green { background: rgba(16, 185, 129, 0.2); color: #34d399; }
  .p-orange { background: rgba(245, 158, 11, 0.2); color: #fbbf24; }
  .p-red { background: rgba(239, 68, 68, 0.2); color: #f87171; }
  .btn { background: rgba(255,255,255,0.1); color: var(--text); border: var(--border); padding: 6px 12px; border-radius: 6px; cursor: pointer; }
  
  .drag-handle { cursor: grab; padding: 5px; background: rgba(255,255,255,0.05); margin-bottom: 10px; display: none; border-radius: 4px; text-align: center; }
  body.theme-custom .drag-handle { display: block; }
  
  .list-item { background: var(--card-bg); border: var(--border); border-radius: 8px; overflow: hidden; }
  .list-head-grid { display: grid; grid-template-columns: auto 1fr auto; gap: 1rem; align-items: center; padding: 10px; cursor: pointer; background: rgba(0,0,0,0.2); transition: background 0.2s; }
  .stat-row { display: flex; gap: 15px; font-size: 0.85rem; flex-wrap: wrap; justify-content: flex-end; }
  .big-num { font-size: 1.8rem; font-weight: 700; }
  .sub-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px; text-align: center; }
  .box { background: rgba(0,0,0,0.2); padding: 8px; border-radius: 6px; }
  .stat-box { border-radius: 8px; padding: 5px; } 
  .lbl { font-size: 0.7rem; color: var(--text-sec); text-transform: uppercase; }
  .c-grid { display: flex; flex-wrap: wrap; gap: 4px; margin-top: 10px; justify-content: center; }
  .c-box { background: rgba(255,255,255,0.05); padding: 4px; text-align: center; border-radius: 4px; font-size: 0.75rem; min-width: 32px; flex: 1; }
  .c-min { box-shadow: inset 0 0 0 1px #3b82f6; } .c-max { box-shadow: inset 0 0 0 1px #ef4444; }
  .c-lbl { display:block; font-size:0.6rem; color:#aaa; margin-bottom:2px; }
  .footer { text-align: center; margin-top: 30px; font-size: 0.8rem; opacity: 0.5; }
  canvas { width: 100%; height: 220px; }
  details { margin-top:10px; }
  #headClock { font-family: 'Courier New', monospace; font-size: 1.1rem; letter-spacing: 1px; color: #cbd5e1; }
  .chart-controls { display:flex; justify-content:space-between; align-items:center; margin-bottom:5px; }
  .err-banner { border:1px solid #ef4444; color:#ef4444; text-align:center; display:none; margin-bottom:15px; background:rgba(239,68,68,0.1); padding:10px; border-radius:8px; }
  .help-sec { font-size:0.8rem; color:#aaa; margin-top:10px; border-top:1px solid #444; padding-top:10px; }
  .exp-zone { border:1px solid #f59e0b; padding:10px; border-radius:5px; background:rgba(245, 158, 11, 0.1); display:none; margin-top:10px; }
  #sysCard { grid-column: 1 / -1; margin-bottom: 20px; background: var(--head-col) !important; border-top:none !important; border-right:none !important; }
  .header-txt { font-size:1.4rem; font-weight:700; background: var(--head-txt); -webkit-background-clip: text; -webkit-text-fill-color: transparent; color: var(--text); }
  .bms-border { border-left: 5px solid transparent; }
  .glow-chg { box-shadow: inset 0 20px 20px -10px rgba(16, 185, 129, 0.3) !important; }
  .glow-dis { box-shadow: inset 0 20px 20px -10px rgba(245, 158, 11, 0.3) !important; }
  .glow-idle { box-shadow: inset 0 20px 20px -10px rgba(59, 130, 246, 0.3) !important; }
  .glow-err { box-shadow: inset 0 20px 20px -10px rgba(239, 68, 68, 0.3) !important; }
  .theme-live #sysCard { background: transparent !important; box-shadow: none; border: none; backdrop-filter: none; }
  .theme-live .stat-box { background: rgba(0,0,0,0.6); box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
  .theme-live .sub-grid .box { background: rgba(0,0,0,0.6); }
  .theme-live details { background: rgba(0,0,0,0.6); border-radius: 12px; padding: 10px; }
 </style>
</head>
<body>
  <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; flex-wrap:wrap; gap:10px;">
     <div class="header-txt">Topband V117</div>
     <div id="headClock">--:--:--</div>
     <div>
       <button class="btn" id="editBtn" style="display:none;" onclick="toggleEdit()">✏️</button>
       <button class="btn" onclick="toggleView()" id="btnView">LIST</button> 
       <button class="btn" onclick="toggleLang()" id="btnLang">DE</button>
     </div>
  </div>
  
  <div id="canErr" class="err-banner">CAN BUS ERROR</div>
  <div id="sysErr" class="err-banner">SYSTEM ERROR (SD?)</div>
  <div id="safeErr" class="err-banner" style="border-color:#f59e0b; color:#f59e0b; background:rgba(245,158,11,0.1);">SAFETY CUTOFF ACTIVE!</div>

  <div class="grid" id="mainGrid">
     <div class="card draggable" id="sysCard" draggable="false">
        <div class="drag-handle">::: Drag System :::</div>
        <div style="display:flex; justify-content:space-between; margin-bottom:15px;" id="sysHead">
           <span style="font-weight:bold;" id="l-sys">SYSTEM TOTAL</span> <span class="pill p-blue" id="sysState">IDLE</span>
        </div>
        <div style="display:flex; flex-wrap:wrap; justify-content:space-around; text-align:center; gap:15px;">
           <div class="stat-box"><div class="big-num" id="t-soc" style="color:#10b981;">-- %</div><div class="lbl" id="l-soc">SOC</div></div>
           <div class="stat-box"><div class="big-num" id="t-pwr">-- W</div><div class="lbl" id="l-pwr">POWER</div></div>
           <div class="stat-box"><div class="big-num" id="t-cur">-- A</div><div class="lbl" id="l-cur">CURRENT</div></div>
           <div class="stat-box"><div class="big-num" id="t-vol">-- V</div><div class="lbl" id="l-vol">VOLTAGE</div></div>
        </div>
        <div class="sub-grid" style="grid-template-columns: repeat(auto-fit, minmax(100px, 1fr));">
            <div class="box"><div class="lbl" id="l-mix">SOH / Temp</div><div id="t-mix">--</div></div>
            <div class="box"><div class="lbl" id="l-cap">Capacity</div><div id="t-cap">--</div></div>
            <div class="box"><div class="lbl" id="l-cnt">Active BMS</div><div id="t-cnt">--</div></div>
            <div class="box"><div class="lbl" id="l-ein">Energy In</div><div id="e-in" style="color:#10b981;">--</div></div>
            <div class="box"><div class="lbl" id="l-eout">Energy Out</div><div id="e-out" style="color:#f59e0b;">--</div></div>
        </div>
        <details open>
           <summary id="l-show" style="cursor:pointer; color:#94a3b8;">Graph</summary>
           <div class="chart-controls">
                <span id="chartTitle" style="font-size:0.8rem; color:#aaa;">POWER (48h)</span>
                <button class="btn" style="padding:2px 8px; font-size:0.7rem;" onclick="toggleChartMode()">48h / 7D</button>
           </div>
           <canvas id="chart"></canvas>
        </details>
     </div>
     <div id="bmsCon" style="display:contents;"></div>
  </div>

  <details class="card" style="margin-top:20px;">
     <summary id="l-set">Settings & Tools</summary>
     <form action="/save" method="POST" style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-top:10px;">
        <input type="number" name="cnt" placeholder="BMS Count" id="i_cnt">
        <input type="number" name="cells" placeholder="Cells (0=Auto)" id="i_cells">
        
        <div style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px; font-weight:bold;">Interface</div>
        <select name="theme" id="i_theme" style="grid-column:1/-1;">
            <option value="0">Modern Glass (Default)</option>
            <option value="1">Retro Dark (Blue/Black)</option>
            <option value="2">Simple High-Contrast</option>
            <option value="3">🔋 Battery Live (Dynamic)</option>
            <option value="4">👾 Cyberpunk HUD</option>
            <option value="5">☁️ Soft UI (Neumorphism)</option>
            <option value="6">🏗️ Custom Dashboard (Drag & Resize)</option>
        </select>

        <div style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px; font-weight:bold;">Time & Date</div>
        <input type="text" name="ntp_svr" placeholder="NTP Server (pool.ntp.org)" id="i_ntp">
        <input type="number" name="tz_off" placeholder="GMT Offset (Hours, e.g. 1)" id="i_tz">

        <div style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px; font-weight:bold;">Monitoring</div>
        <div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="vic_en" id="i_vic_en"> Enable Victron CAN (Send Data)</label></div>

        <div style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px; font-weight:bold;">SD Card</div>
        <div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="sd_en" id="i_sd"> Enable SD Logging</label></div>
        <div id="sd_ctrl" style="grid-column:1/-1; display:flex; gap:10px; align-items:center;">
             <span style="font-size:0.8rem; color:#aaa;" id="sd_stat">Status: --</span>
             <button type="button" class="btn" onclick="window.open('/sd/download')">CSV</button>
             <button type="button" class="btn" style="background:#ef4444;" onclick="if(confirm('Delete Log?')) fetch('/sd/clear').then(()=>alert('Deleted'))">DEL</button>
        </div>

        <div style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px; font-weight:bold;">MQTT Settings</div>
        <div style="grid-column:1/-1;"><label class="check-lbl"><input type="checkbox" name="mq_en" id="i_mq_en"> Enable MQTT</label></div>
        <input type="text" name="mq_ip" placeholder="MQTT Broker IP" id="i_mq_ip">
        <input type="number" name="mq_pt" placeholder="Port (1883)" value="1883" id="i_mq_pt">
        <input type="text" name="mq_us" placeholder="User" id="i_mq_us">
        <input type="password" name="mq_pw" placeholder="Password" id="i_mq_pw">

        <label style="grid-column:1/-1; border-top:1px solid #ffffff20; margin-top:10px; padding-top:10px;">
           <label class="check-lbl">
               <input type="checkbox" name="exp" id="chk_exp" onclick="toggleExp(this)"> <span id="l-exp" style="color:#f59e0b; font-weight:bold;">Enable Expert Mode</span>
           </label>
           <div style="font-size:0.7rem; color:#888;">Enable to change Charge Voltage/Amps.</div>
        </label>
        
        <div id="exp_box" class="exp-zone">
           <div style="color:#f59e0b; font-size:0.8rem; margin-bottom:5px;">WARNING: WRONG SETTINGS CAN DAMAGE BATTERY!</div>
           <div style="display:grid; grid-template-columns:1fr 1fr; gap:5px;">
               <label>Charge Voltage (V)</label>
               <input type="number" step="0.1" name="cvl" id="i_cvl" placeholder="52.5">
               <label>Charge Amps (A)</label>
               <input type="number" step="0.1" name="chg" id="i_chg">
               <label>Discharge Amps (A)</label>
               <input type="number" step="0.1" name="dis" id="i_dis">
           </div>
        </div>
        
        <button type="submit" class="btn" style="grid-column:1/-1; background:#10b981;" id="l-save">SAVE REBOOT</button>
     </form>
     
     <div class="help-sec">
         <b>Legend / Colors:</b><br>
         <span style="color:#10b981">GREEN</span> = Charge / Online / Normal<br>
         <span style="color:#f59e0b">ORANGE</span> = Discharge / Warning<br>
         <span style="color:#ef4444">RED</span> = Error / Offline<br><br>
         <b>Onboard LED Status:</b><br>
         <span style="color:#60a5fa">BLUE (Solid)</span> = Booting<br>
         <span style="color:#34d399">GREEN (Solid)</span> = WiFi Connected<br>
         <span style="color:#ef4444">RED (Solid)</span> = WiFi Failed (AP Mode)<br>
         <span style="color:#34d399">GREEN (Blink)</span> = CAN TX OK<br>
         <span style="color:#ef4444">RED (Blink)</span> = CAN TX Error
     </div>

     <div id="log" style="font-family:monospace; font-size:0.7rem; height:100px; overflow-y:scroll; background:rgba(0,0,0,0.3); padding:5px; margin-top:10px;"></div>
  </details>

  <div class="footer">
    <div style="text-align:center; margin-top:30px; opacity:0.6; font-size:0.8rem;">
       <span id="l-disc">PRIVATE USE ONLY. NO LIABILITY ACCEPTED.<br>This project is open source and not affiliated with Topband or Victron Energy.</span>
       <br><a href="https://github.com/atomi23/Topband-BMS-to-CAN" style="color:#3b82f6;">GitHub Project</a>
    </div>
  </div>

  <script>
    let view='grid'; let lang='de'; let chartMode='power'; let initDone=false; let editMode=false;
    const ctx=document.getElementById('chart').getContext('2d');
    const TR = {
        de: { 'l-sys':'SYSTEM GESAMT', 'l-soc':'KAPAZITÄT', 'l-pwr':'LEISTUNG', 'l-cur':'STROM', 'l-vol':'SPANNUNG', 'l-mix':'SOH / TEMP', 'l-cap':'REST / TOTAL', 'l-cnt':'AKTIVE PACKS', 'l-ein':'ENERGIE EIN', 'l-eout':'ENERGIE AUS', 'l-show':'Diagramm', 'l-set':'Einstellungen', 'l-exp':'Experten Modus aktivieren', 'l-save':'SPEICHERN & NEUSTART', 'l-disc':'NUR FÜR PRIVATEN GEBRAUCH. KEINE HAFTUNG.<br>Kein kommerzieller Nutzen. Open Source Projekt.' },
        en: { 'l-sys':'SYSTEM TOTAL', 'l-soc':'SOC', 'l-pwr':'POWER', 'l-cur':'CURRENT', 'l-vol':'VOLTAGE', 'l-mix':'SOH / TEMP', 'l-cap':'CAPACITY', 'l-cnt':'ACTIVE PACKS', 'l-ein':'ENERGY IN', 'l-eout':'ENERGY OUT', 'l-show':'Graph', 'l-set':'Settings', 'l-exp':'Enable Expert Mode', 'l-save':'SAVE REBOOT', 'l-disc':'PRIVATE USE ONLY. NO LIABILITY ACCEPTED.<br>No Commercial Use. Open Source Project.' }
    };
    const ST_TXT = { de: { 'IDLE':'STANDBY', 'CHG':'LADEN', 'DIS':'ENTLADEN', 'OFF':'OFF' }, en: { 'IDLE':'IDLE', 'CHG':'CHARGING', 'DIS':'DISCHARGING', 'OFF':'OFF' } };
    
    function updateClock() { document.getElementById('headClock').innerText = new Date().toLocaleTimeString(); }
    setInterval(updateClock, 1000); 

    function toggleLang() { lang=(lang==='de'?'en':'de'); updateText(); }
    function updateText() { const t=TR[lang]; document.getElementById('btnLang').innerText=lang.toUpperCase(); for(const k in t){ const e=document.getElementById(k); if(e)e.innerHTML=t[k]; } }
    function toggleDetails(el) { if(!editMode) { let det = el.parentElement.querySelector('.b-det'); if(det) det.style.display = (det.style.display==='none' ? 'block' : 'none'); } }
    function toggleChartMode() { chartMode = (chartMode === 'power') ? 'energy' : 'power'; document.getElementById('chartTitle').innerText = (chartMode === 'power') ? "POWER (48h)" : "ENERGY (7 Days)"; update(); }

    function toggleExp(el) {
        let box = document.getElementById('exp_box');
        if(el.checked) {
            if(confirm("WARNING / ACHTUNG:\n\nChanging these values can destroy your battery or void warranty.\nSafety Cutoff (>56.5V) remains active.\n\nContinue?")) { box.style.display = 'block'; } 
            else { el.checked = false; box.style.display = 'none'; }
        } else { box.style.display = 'none'; }
    }
    
    // --- DRAG & DROP ---
    let dragSrc = null;
    function dragStart(e) { dragSrc = this; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/html', this.innerHTML); this.style.opacity = '0.4'; }
    function dragOver(e) { if (e.preventDefault) { e.preventDefault(); } e.dataTransfer.dropEffect = 'move'; return false; }
    function dragDrop(e) { if (dragSrc != this) { let srcHTML = dragSrc.innerHTML; let tgtHTML = this.innerHTML; dragSrc.innerHTML = tgtHTML; this.innerHTML = srcHTML; saveLayout(); } return false; }
    function dragEnd(e) { this.style.opacity = '1'; }
    function enableDrag() {
        let items = document.querySelectorAll('.draggable');
        items.forEach(item => { item.setAttribute('draggable', true); item.addEventListener('dragstart', dragStart); item.addEventListener('dragover', dragOver); item.addEventListener('drop', dragDrop); item.addEventListener('dragend', dragEnd); });
    }
    function toggleEdit() {
        editMode = !editMode;
        let btn = document.getElementById('editBtn'); btn.style.background = editMode ? '#ef4444' : 'rgba(255,255,255,0.1)';
        document.body.classList.toggle('edit-active');
    }
    function saveLayout() { }

    function drawChart(d, ts) {
        if(!d || d.length<2) return;
        const c=ctx.canvas; c.width=c.clientWidth; c.height=220;
        const w=c.width, h=c.height, pad=60, bPad=30, tPad=30; 
        ctx.clearRect(0,0,w,h);
        
        let min=Math.min(...d, -100); let max=Math.max(...d, 100);
        let r = max - min; min -= r*0.1; max += r*0.1; r = max - min;
        
        const drawH = h - bPad - tPad;
        const getY = (v) => tPad + drawH - ((v-min)/r * drawH);
        const y0 = getY(0);

        ctx.textAlign = "right"; ctx.textBaseline = "middle";
        ctx.fillStyle = "#aaa"; ctx.font = '10px sans-serif';
        const steps = 4;
        for(let i=0; i<=steps; i++) {
             let val = min + (r * (i/steps)); let y = getY(val);
             ctx.beginPath(); ctx.strokeStyle = "rgba(255,255,255,0.05)"; ctx.moveTo(pad, y); ctx.lineTo(w, y); ctx.stroke();
             ctx.fillText(Math.round(val), pad-5, y);
        }

        let fillGrd = ctx.createLinearGradient(0, 0, 0, h - bPad);
        let zeroRatio = y0 / (h - bPad); if(zeroRatio<0) zeroRatio=0; if(zeroRatio>1) zeroRatio=1;
        fillGrd.addColorStop(0, 'rgba(16, 185, 129, 0.5)'); fillGrd.addColorStop(zeroRatio, 'rgba(16, 185, 129, 0.5)');
        fillGrd.addColorStop(zeroRatio, 'rgba(245, 158, 11, 0.5)'); fillGrd.addColorStop(1, 'rgba(245, 158, 11, 0.5)');

        let strokeGrd = ctx.createLinearGradient(0, 0, 0, h - bPad);
        strokeGrd.addColorStop(0, '#10b981'); strokeGrd.addColorStop(zeroRatio, '#10b981');
        strokeGrd.addColorStop(zeroRatio, '#f59e0b'); strokeGrd.addColorStop(1, '#f59e0b');

        ctx.beginPath(); ctx.strokeStyle = '#666'; ctx.lineWidth = 1; ctx.setLineDash([5, 5]);
        ctx.moveTo(pad, y0); ctx.lineTo(w, y0); ctx.stroke(); ctx.setLineDash([]);
        
        const stepX = (w-pad) / (d.length - 1);
        ctx.beginPath(); ctx.moveTo(pad, y0); 
        d.forEach((v,i)=>{ ctx.lineTo(pad+(i*stepX), getY(v)); });
        ctx.lineTo(w, y0); ctx.fillStyle = fillGrd; ctx.fill();

        ctx.beginPath();
        d.forEach((v,i)=>{ let x=pad+(i*stepX); let y=getY(v); if(i==0) ctx.moveTo(x,y); else ctx.lineTo(x,y); });
        ctx.lineWidth=2; ctx.strokeStyle = strokeGrd; ctx.stroke();

        ctx.fillStyle = '#666'; ctx.textAlign = 'center'; ctx.textBaseline = "top";
        for(let i=d.length-1; i>=0; i-=20) { 
             let x = pad + (i*stepX);
             let ptTime = ts - ((d.length - 1 - i) * 180);
             let date = new Date(ptTime * 1000);
             let hrs = date.getHours(); 
             ctx.beginPath(); ctx.strokeStyle = 'rgba(255,255,255,0.05)'; ctx.moveTo(x, 0); ctx.lineTo(x, h-bPad); ctx.stroke();
             ctx.beginPath(); ctx.strokeStyle = '#666'; ctx.moveTo(x, h-bPad); ctx.lineTo(x, h-bPad+5); ctx.stroke();
             if(hrs % 2 === 0 || i === d.length-1) {
                 let label = hrs.toString().padStart(2,'0') + ":" + date.getMinutes().toString().padStart(2,'0');
                 if(hrs === 0) { label = date.getDate().toString().padStart(2,'0') + "." + (date.getMonth()+1).toString().padStart(2,'0') + "."; ctx.fillStyle = '#ccc'; } else { ctx.fillStyle = '#666'; }
                 ctx.fillText(label, x, h-bPad+5);
             }
        }
    }

    function drawBarChart(din, dout) {
        if(!din || !dout) return;
        const c=ctx.canvas; c.width=c.clientWidth; c.height=220;
        const w=c.width, h=c.height, pad=60, bPad=30, tPad=30; 
        ctx.clearRect(0,0,w,h);
        
        let maxVal = 0; for(let i=0; i<7; i++) { if(din[i]>maxVal) maxVal=din[i]; if(dout[i]>maxVal) maxVal=dout[i]; }
        if(maxVal<1) maxVal=1; else maxVal*=1.1;
        const drawH = h - bPad - tPad;
        const getY = (v) => tPad + drawH - (v/maxVal * drawH);
        
        ctx.strokeStyle = '#333'; ctx.lineWidth=1;
        ctx.beginPath(); ctx.moveTo(pad, getY(0)); ctx.lineTo(w, getY(0)); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(pad, getY(maxVal)); ctx.lineTo(w, getY(maxVal)); ctx.stroke();
        ctx.fillStyle = '#aaa'; ctx.textAlign='right'; ctx.font='10px sans-serif';
        ctx.fillText(maxVal.toFixed(1)+" kWh", pad-5, getY(maxVal)+3); ctx.fillText("0", pad-5, getY(0)+3);

        const barW = (w-pad)/7; const barGap = 4; const subBarW = (barW - barGap*2)/2;
        for(let i=0; i<7; i++) {
            let xBase = pad + ((6-i) * barW) + barGap; 
            let hIn = (h-bPad) - getY(din[i]); ctx.fillStyle = '#10b981'; ctx.fillRect(xBase, getY(din[i]), subBarW, hIn);
            if(din[i]>0.1) { ctx.fillStyle='#fff'; ctx.textAlign='center'; ctx.fillText(din[i].toFixed(1), xBase+subBarW/2, getY(din[i])-2); }
            let hOut = (h-bPad) - getY(dout[i]); ctx.fillStyle = '#f59e0b'; ctx.fillRect(xBase + subBarW, getY(dout[i]), subBarW, hOut);
            if(dout[i]>0.1) { ctx.fillStyle='#fff'; ctx.textAlign='center'; ctx.fillText(dout[i].toFixed(1), xBase+subBarW+subBarW/2, getY(dout[i])-2); }
            ctx.fillStyle = '#999'; ctx.textAlign='center';
            let label = (i==0) ? "HEUTE" : "-"+i; ctx.fillText(label, xBase + subBarW, h-5);
        }
    }

    function update() {
      fetch('/data').then(r=>r.json()).then(d=>{
        document.getElementById('t-soc').innerText=d.victron.avg_soc.toFixed(1)+'%';
        document.getElementById('t-pwr').innerText=d.victron.total_power.toFixed(0)+' W';
        document.getElementById('t-cur').innerText=d.victron.total_amps.toFixed(1)+' A';
        document.getElementById('t-vol').innerText=d.victron.voltage.toFixed(2)+' V';
        document.getElementById('t-cap').innerText=d.victron.rem_cap.toFixed(0)+'/'+d.victron.total_cap.toFixed(0)+' Ah';
        document.getElementById('t-mix').innerText=d.victron.avg_soh.toFixed(0)+'% / '+d.victron.avg_temp.toFixed(1)+'°C';
        document.getElementById('t-cnt').innerText=d.victron.active;
        document.getElementById('e-in').innerText=d.energy.in.toFixed(1)+' kWh';
        document.getElementById('e-out').innerText=d.energy.out.toFixed(1)+' kWh';
        
        const sc=document.getElementById('sysCard'); const ss=document.getElementById('sysState');
        let sKey = 'IDLE';
        let borderCol = '#6366f1'; 
        // Logic for Border Colors / Glow
        let glowClass = 'glow-idle';
        if(d.victron.total_power>50) { borderCol='#10b981'; glowClass='glow-chg'; ss.className='pill p-green'; sKey='CHG'; }
        else if(d.victron.total_power<-50) { borderCol='#f59e0b'; glowClass='glow-dis'; ss.className='pill p-orange'; sKey='DIS'; }
        else { ss.className='pill p-blue'; }
        ss.innerText = ST_TXT[lang][sKey];
        
        // --- COLORED SYSTEM BORDER ---
        let th = document.getElementById('i_theme').value;
        if(th != "1" && th != "3" && th != "4" && th != "6") { // Standard, Simple, SoftUI
             let sh = document.getElementById('sysHead');
             sh.style.borderTop = "4px solid " + borderCol;
             sc.style.borderTop = "5px solid " + borderCol;
             sc.style.borderRight = "5px solid " + borderCol;
             sc.className = "card draggable " + glowClass; 
        } else {
             sc.className = "card draggable"; 
        }

        // --- LIVE THEME (DYNAMIC) ---
        if(th == "3") {
             if(document.body.className !== 'theme-live') document.body.className = 'theme-live';
             let soc = d.victron.avg_soc;
             let col = (soc < 20) ? "#991b1b" : (soc < 50) ? "#b45309" : "#065f46";
             document.body.style.background = `linear-gradient(to top, ${col} ${soc}%, #111 ${soc}%)`;
        } else if(th == "4") { document.body.className = 'theme-cyber'; }
        else if(th == "6") { document.body.className = 'theme-custom'; document.getElementById('editBtn').style.display='inline-block'; enableDrag(); }
        else { document.body.className = ''; document.getElementById('editBtn').style.display='none'; }

        // HIDE ERROR IF VICTRON DISABLED (Monitoring Mode)
        if(d.can_error && d.config.vic_en) { 
             document.getElementById('canErr').style.display='block'; 
             document.getElementById('canErr').innerText='CAN ERROR: '+d.can_status; 
        } else { document.getElementById('canErr').style.display='none'; }
        
        if(d.sd_err || (document.getElementById('i_sd').checked && !d.sd_ok)) {
            let el = document.getElementById('sysErr'); el.style.display='block'; el.innerText = d.sd_err ? "SD WRITE ERROR" : "NO SD CARD";
        } else { document.getElementById('sysErr').style.display='none'; }
        
        if(d.safe_lock) document.getElementById('safeErr').style.display='block'; else document.getElementById('safeErr').style.display='none';
        
        document.getElementById('sd_stat').innerText = "Status: " + (d.sd_ok ? "OK" : "NO CARD");
        document.getElementById('log').innerHTML=d.log;
        
        // --- AUTOFILL CONFIG ---
        if(!initDone && d.config) {
            document.getElementById('i_cnt').value = d.config.cnt;
            document.getElementById('i_cells').value = d.config.cells;
            document.getElementById('i_chg').value = d.config.chg;
            document.getElementById('i_dis').value = d.config.dis;
            document.getElementById('i_cvl').value = d.config.cvl;
            document.getElementById('chk_exp').checked = d.config.exp;
            if(d.config.exp) document.getElementById('exp_box').style.display='block';
            
            document.getElementById('i_theme').value = d.config.theme;
            document.getElementById('i_ntp').value = d.config.ntp;
            document.getElementById('i_tz').value = d.config.tz;
            document.getElementById('i_sd').checked = d.config.sd;
            document.getElementById('i_vic_en').checked = d.config.vic_en;
            document.getElementById('i_mq_en').checked = d.config.mq_en;
            document.getElementById('i_mq_ip').value = d.config.mq_ip;
            document.getElementById('i_mq_pt').value = d.config.mq_pt;
            document.getElementById('i_mq_us').value = d.config.mq_us;
            initDone = true;
        }

        const con = document.getElementById('bmsCon');
        d.bms.forEach((b,i)=>{
           let el = document.getElementById('bms-'+i);
           let cg=''; b.cells.forEach(v=>{ cg+=`<div class="c-box ${v==b.min_cell?'c-min':''} ${v==b.max_cell?'c-max':''}">${v.toFixed(3)}</div>`; });
           let tg=''; b.temps.forEach(t=>{ tg+=`<div class="c-box"><div class="c-lbl">${t.lbl}</div>${t.val.toFixed(1)}°</div>`; });
           let diff = (b.max_cell - b.min_cell).toFixed(3);
           let pwr = Math.round(b.pack_v * b.current);
           let bKey='IDLE', pc='p-blue';
           let bGlow = 'glow-idle';
           let bCol = 'transparent';
           if(b.current>0.5){bKey='CHG'; pc='p-green'; bGlow='glow-chg'; bCol='#10b981';} 
           else if(b.current<-0.5){bKey='DIS'; pc='p-orange'; bGlow='glow-dis'; bCol='#f59e0b';}
           if(!b.online){bKey='OFF'; pc='p-red'; bGlow='glow-err'; bCol='#ef4444';}
           let stTxt = ST_TXT[lang][bKey];

           let headHTML = '';
           if(view==='list') {
               headHTML = `<div class="list-head-grid bms-border" style="border-left: 5px solid ${bCol};" onclick="toggleDetails(this)">
                   <div><span style="font-weight:bold; opacity:0.7;">#${i}</span></div>
                   <div style="font-size:1.2rem; font-weight:bold; color:${bCol}">${b.soc}%</div>
                   <div class="stat-row">
                       <span>${b.pack_v.toFixed(2)}V</span> <span>${b.current.toFixed(1)}A</span> <span>${pwr}W</span>
                       <span style="opacity:0.7">SOH:${b.soh}%</span> <span style="opacity:0.7">T:${b.avg_temp.toFixed(1)}°</span>
                       <span style="opacity:0.7">D:${diff}</span>
                   </div>
                   <div><span class="pill ${pc}">${stTxt}</span></div>
               </div>`;
           } else {
               headHTML = `<div style="font-weight:bold; display:flex; justify-content:space-between;"><span>BMS #${i}</span> <span class="pill ${pc}">${stTxt}</span></div>
                           <div style="margin-top:5px; font-size:0.9rem;">${b.soc}% | ${b.pack_v.toFixed(2)}V | ${b.current.toFixed(1)}A | ${pwr}W</div>`;
           }
           let bodyHTML = `<div class="b-det" style="${view==='list'?'display:none; padding:10px; border-top:1px solid rgba(255,255,255,0.1);':''}">
                      <div class="sub-grid" style="margin-bottom:10px;"> <div class="box"><span>Diff:</span> ${diff} V</div> <div class="box"><span>SOH:</span> ${b.soh}%</div> </div>
                      <details><summary>Cells</summary><div class="c-grid">${cg}</div></details> <div class="c-grid">${tg}</div>
                   </div>`;
           if(!el) {
               let cls = view==='list' ? 'list-item' : 'card draggable '+bGlow;
               let style = `margin-bottom:10px; ${view!=='list'?'padding:10px; border-top: 5px solid '+bCol+'; border-right: 5px solid '+bCol:''}`;
               let h = `<div id="bms-${i}" class="${cls}" style="${style}" draggable="false">
                        <div class="drag-handle">::: Drag BMS #${i} :::</div>
                        <div id="bms-h-${i}">${headHTML}</div> <div id="bms-b-${i}">${bodyHTML}</div>
                       </div>`;
               con.insertAdjacentHTML('beforeend', h);
           } else {
               if(view!=='list' && th != "1" && th != "3" && th != "4") { 
                   el.className = 'card draggable ' + bGlow; 
                   // Update Borders Dynamic
                   el.style.borderTop = "5px solid " + bCol;
                   el.style.borderRight = "5px solid " + bCol;
                   el.style.borderLeft = "none";
               } else {
                   el.className = (view==='list' ? 'list-item' : 'card draggable');
                   el.style.borderTop = "none";
                   el.style.borderRight = "none";
               }
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
        
        if(chartMode === 'power') { if(d.history && d.ts) drawChart(d.history, d.ts); } 
        else { if(d.days_in && d.days_out) drawBarChart(d.days_in, d.days_out); }
      });
    }
    function toggleView(){ view=(view==='grid'?'list':'grid'); document.getElementById('btnView').innerText=(view==='grid'?'LIST':'GRID'); const m=document.getElementById('mainGrid'); if(view==='list'){ m.style.display='flex'; m.style.flexDirection='column'; } else { m.style.display='grid'; } document.getElementById('bmsCon').innerHTML=''; update(); }
    setInterval(update, 2000); updateText(); updateClock();
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", h);
}

void setup() {
  Serial.begin(115200); Serial.println("\n=== V117 STABLE CORE ===");
  #if BOARD_TYPE == 2
  pinMode(PIN_RS485_DIR, OUTPUT); digitalWrite(PIN_RS485_DIR, LOW);
  #else
  pinMode(PIN_PWR_BOOST, OUTPUT); digitalWrite(PIN_PWR_BOOST, HIGH);
  pinMode(PIN_485_EN, OUTPUT); digitalWrite(PIN_485_EN, HIGH);
  pinMode(PIN_485_CB, OUTPUT); digitalWrite(PIN_485_CB, HIGH);
  #endif
  pixels.begin(); pixels.setBrightness(20); setLed(0, 0, 255);
  preferences.begin("gateway", false);
  g_bms_count = preferences.getInt("cnt", 2); g_force_cell_count = preferences.getInt("cells", 0);
  g_charge_amps = preferences.getFloat("chg", 20.0); g_discharge_amps = preferences.getFloat("dis", 30.0);
  g_expert_mode = preferences.getBool("exp", false);
  if(g_expert_mode) g_cvl_voltage = preferences.getFloat("cvl", STD_CHARGE_VOLT); else g_cvl_voltage = STD_CHARGE_VOLT;
  g_sd_enable = preferences.getBool("sd_en", false); g_mqtt_enable = preferences.getBool("mq_en", false);
  g_theme_id = preferences.getInt("theme", 0);
  g_victron_enable = preferences.getBool("vic_en", true); 
  g_mqtt_server = preferences.getString("mq_ip", ""); g_mqtt_port = preferences.getInt("mq_pt", 1883);
  g_mqtt_user = preferences.getString("mq_us", ""); g_mqtt_pass = preferences.getString("mq_pw", "");
  g_ntp_server = preferences.getString("ntp", "pool.ntp.org"); g_timezone_offset = preferences.getInt("tz", 1);
  preferences.end();
  loadHistory(); initSD();
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  #if ARDUINO_ESP32_MAJOR >= 2
  RS485.setRxBufferSize(1024);
  #endif
  twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_cfg = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_cfg, &t_cfg, &f_cfg); twai_start();
  
  // WIFI MANAGER SETUP
  WiFiManager wm;
  // wm.resetSettings(); // ONLY UNCOMMENT FOR DEBUGGING IF LOOP HAPPENS
  wm.setConnectTimeout(30); // 30 seconds for router connection
  wm.setConfigPortalTimeout(180); // 3 minutes portal timeout

  // If fixed credentials are provided, try them first (Legacy mode)
  if (String(FIXED_SSID) != "") {
      WiFi.mode(WIFI_STA);
      WiFi.begin(FIXED_SSID, FIXED_PASS);
      int tr=0; while(WiFi.status() != WL_CONNECTED && tr<20) { delay(500); tr++; }
  }

  // AutoConnect: Connect to known WiFi or start AP "Victron-Gateway-Setup"
  if(!wm.autoConnect("Victron-Gateway-Setup")) {
      Serial.println("Failed to connect and hit timeout");
      // Reset or sleep here if you want
      // ESP.restart(); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    setLed(0, 255, 0); 
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    if (g_mqtt_enable && g_mqtt_server != "") mqtt.setServer(g_mqtt_server.c_str(), g_mqtt_port);
    configTime(g_timezone_offset * 3600, 0, g_ntp_server.c_str());
  } else { setLed(255, 0, 0); }
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/sim", handleSim); server.on("/save", handleSave);
  server.on("/sd/download", HTTP_GET, handleSDDownload); server.on("/sd/clear", HTTP_GET, handleSDClear);
  server.begin();
  
  // WDT REMOVED HERE TO PREVENT DOUBLE INIT CRASH
  esp_task_wdt_add(NULL);
}

void loop() {
  // FEED THE DOG!
  esp_task_wdt_reset(); 
  
  server.handleClient();
  unsigned long now = millis();
  
  if (now - last_hist_time > HISTORY_INTERVAL) { last_hist_time = now; updateHistory(); }
  calculateEnergy();
  if (g_sd_enable && (now - last_sd_time > SD_LOG_INTERVAL)) { last_sd_time = now; writeLogToSD(); }
  mqttReconnect();
  if (g_mqtt_enable && WiFi.status() == WL_CONNECTED && mqtt.connected()) { mqtt.loop(); if (now - last_mqtt_time > MQTT_INTERVAL) { last_mqtt_time = now; sendMqttData(); } }

  if (!simulation_active && (now - last_poll_time > POLL_INTERVAL)) {
    last_poll_time = now;
    for (int i = 0; i < g_bms_count; i++) {
      
      esp_task_wdt_reset(); // Feed before work

      #if BOARD_TYPE == 2
      digitalWrite(PIN_RS485_DIR, HIGH);
      #endif
      RS485.flush(); while (RS485.available()) RS485.read();
      RS485.print(POLL_CMDS[i]); RS485.flush();
      #if BOARD_TYPE == 2
      digitalWrite(PIN_RS485_DIR, LOW);
      #endif
      
      unsigned long start = millis(); String resp = "";
      while (millis() - start < RS485_TIMEOUT) {
        // Feed WHILE waiting (Critical Fix)
        esp_task_wdt_reset(); 
        
        if (RS485.available()) { char c = RS485.read(); if (c == '\r') break; if (c != '~') resp += c; } 
        else yield(); 
      }
      
      if (resp.length() > 0) parseTopband(resp, i);
      else addToLog("Timeout BMS " + String(i), true);
      
      delay(BUS_GUARD_TIME);
    }
  }
  
  if (g_victron_enable && now - last_can_time > 500) {
    last_can_time = now;
    calculateVictronData();
    sendVictronCAN();
    
    if(sys_error_state == 1) { static bool t; t=!t; setLed(t?255:0, 0, 0); } 
    else if(sys_error_state == 2) { static bool t; t=!t; setLed(t?150:0, 0, t?255:0); } 
    else if(victronData.activePacks == 0) { setLed(50, 0, 0); } 
    else { static bool t; t=!t; setLed(0, t?50:0, 0); } 
  }
}