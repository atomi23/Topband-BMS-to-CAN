#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <driver/twai.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <Update.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <SPI.h>
#include <SD.h>

// =====================================================================
// 1. BOARD AUSWAHL & WLAN
// =====================================================================
// 1 = LILYGO T-CAN485
// 2 = WAVESHARE ESP32-S3-RS485-CAN
#define BOARD_TYPE 1 

const char* FIXED_SSID = "yourssid";     
const char* FIXED_PASS = "yourpswd";
const char* HOSTNAME   = "victron-gateway"; 

// =====================================================================
// PINS & DEFINES
// =====================================================================
#if BOARD_TYPE == 1 // LILYGO
    #define RS485_TX_PIN   22
    #define RS485_RX_PIN   21
    #define PIN_PWR_BOOST  16 
    #define PIN_485_EN     19 
    #define PIN_485_CB     17 
    #define CAN_TX_PIN     27
    #define CAN_RX_PIN     26
    #define PIN_CAN_MODE   23 
    #define LED_PIN        4
    #define SD_MISO        2
    #define SD_MOSI        15
    #define SD_SCLK        14
    #define SD_CS          13
#elif BOARD_TYPE == 2 // WAVESHARE S3
    #define RS485_TX_PIN   17
    #define RS485_RX_PIN   18
    #define PIN_RS485_DIR  21 
    #define CAN_TX_PIN     15
    #define CAN_RX_PIN     16
    #define LED_PIN        38
    #define SD_MISO        -1 
    #define SD_MOSI        -1
    #define SD_SCLK        -1
    #define SD_CS          -1
#endif

#define NUM_LEDS         1
#define MAX_BMS          16
#define RS485_BAUD       9600

// TIMING
#define POLL_INTERVAL    3000  
#define RS485_TIMEOUT    600   
#define BUS_GUARD_TIME   200   
#define WDT_TIMEOUT      30 
#define HISTORY_LEN      96
#define HISTORY_INTERVAL 900000 
#define SD_LOG_INTERVAL  60000  
#define MQTT_INTERVAL    5000

const char* POLL_CMDS[16] = {
  "~21004642E00200FD36\r", "~21014642E00201FD34\r", "~21024642E00202FD32\r", "~21034642E00203FD30\r",
  "~21044642E00204FD2E\r", "~21054642E00205FD2C\r", "~21064642E00206FD2A\r", "~21074642E00207FD28\r",
  "~21084642E00208FD26\r", "~21094642E00209FD24\r", "~210A4642E0020AFD14\r", "~210B4642E0020BFD12\r",
  "~210C4642E0020CFD10\r", "~210D4642E0020DFD0E\r", "~210E4642E0020EFD0C\r", "~210F4642E0020FFD0A\r"
};

WebServer server(80);
Preferences preferences;
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial RS485(1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// GLOBAL VARS
int   g_bms_count        = 2;
float g_charge_amps      = 20.0;
float g_discharge_amps   = 30.0;
int   g_force_cell_count = 15;
float g_cvl_voltage      = 52.5; 
bool  g_expert_mode      = false;
bool  g_sd_enable        = false;
String g_mqtt_server     = "";
int    g_mqtt_port       = 1883;
String g_mqtt_user       = "";
String g_mqtt_pass       = "";
bool   g_mqtt_enable     = false;

bool  simulation_active = false;
unsigned long last_poll_time = 0;
unsigned long last_can_time  = 0;
unsigned long last_mqtt_time = 0;
unsigned long last_sd_time   = 0;
unsigned long last_hist_time = 0;

String debug_log = "";
String debug_can_status = "Init...";
bool   can_error_flag = false;
bool   sd_ok = false;

// HISTORY
int16_t powerHistory[HISTORY_LEN]; 
int     historyIdx = 0;
double  energy_charged_kwh = 0;
double  energy_discharged_kwh = 0;
unsigned long last_energy_calc = 0;
int     current_day = -1;
unsigned long stat_rx_count = 0;
unsigned long stat_tx_count = 0;

// DATA
struct BMSData {
  bool valid;
  float voltage; float current; int soc; int soh;
  float rem_ah; float full_ah;
  float cells[16]; int cell_count;
  float temps[8];  int temp_count;
  float minCellV; float maxCellV; float avgCellV;
  int minCellIdx; int maxCellIdx; float maxTemp; float avgTemp;
  unsigned long last_seen;
};

BMSData bms[MAX_BMS];

struct {
  float totalCurrent; float avgVoltage; float avgSOC; float avgSOH;
  float maxChargeCurrent; float maxDischargeCurrent; float avgTemp;
  float totalCapacity; float totalPower;
  int activePacks;
} victronData;

// =====================================================================
// HELFER FUNKTIONEN
// =====================================================================
void setLed(int r, int g, int b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b)); pixels.show();
}

String getTimeStr() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return String(millis()/1000) + "s";
    char timeStringBuff[20];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void addToLog(String msg, bool error) {
    String clr = error ? "log-err" : "log-ok"; 
    String entry = "<div class='" + clr + "'>[" + getTimeStr() + "] " + msg + "</div>";
    if (debug_log.length() > 6000) debug_log = ""; 
    debug_log = entry + debug_log;
    Serial.println(msg); 
}

uint16_t u16(const uint8_t* b, int o) { return (b[o] << 8) | b[o + 1]; }
int16_t  s16(const uint8_t* b, int o) { uint16_t v = u16(b, o); return (v > 32767) ? (v - 65536) : v; }

uint8_t hex2int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0xFF;
}

// =====================================================================
// PARSER (RS485)
// =====================================================================
void parseTopband(String raw, int addr) {
  if (raw.length() < 20) { addToLog("BMS " + String(addr) + ": Kurz", true); return; }
  stat_rx_count++;

  uint8_t b[512];
  int blen = 0;
  for (int i = 0; i < raw.length() - 1; i += 2) {
    if (blen >= 511) break;
    uint8_t h = hex2int(raw[i]);
    uint8_t l = hex2int(raw[i+1]);
    if (h == 0xFF || l == 0xFF) return;
    b[blen++] = (h << 4) | l;
  }

  int idx = -1;
  for (int i = 0; i < blen - 1; i++) {
    if (b[i] == 0xD0 && b[i+1] == 0x7C) { idx = i; break; }
  }
  if (idx == -1) return;

  int p = idx + 2; p++; 
  int cells = b[p]; p++;
  if (g_force_cell_count > 0) cells = g_force_cell_count; 

  bms[addr].cell_count = cells;
  float vSum = 0;
  bms[addr].minCellV = 99.0; bms[addr].maxCellV = 0.0;

  for (int i = 0; i < cells; i++) {
    if (p + 1 >= blen) return;
    float v = u16(b, p) / 1000.0;
    bms[addr].cells[i] = v;
    if(v > 0.1) {
        vSum += v;
        if(v < bms[addr].minCellV) { bms[addr].minCellV = v; bms[addr].minCellIdx = i+1; }
        if(v > bms[addr].maxCellV) { bms[addr].maxCellV = v; bms[addr].maxCellIdx = i+1; }
    }
    p += 2;
  }
  bms[addr].avgCellV = (cells>0) ? vSum/cells : 0;

  if (p < blen) {
    int t_cnt = b[p]; p++;
    if (t_cnt > 8) t_cnt = 8;
    bms[addr].temp_count = t_cnt;
    float max_t = -99; float sum_t = 0;
    for (int i = 0; i < t_cnt; i++) {
      if (p + 1 >= blen) break;
      float t = (u16(b, p) - 2731) / 10.0;
      bms[addr].temps[i] = t;
      sum_t += t;
      if(t > max_t) max_t = t;
      p += 2;
    }
    bms[addr].maxTemp = max_t;
    bms[addr].avgTemp = (t_cnt > 0) ? sum_t / t_cnt : 0;
  }

  if (p + 10 < blen) {
      bms[addr].current = s16(b, p) * 0.01; p += 2;
      bms[addr].voltage = u16(b, p) * 0.01; p += 2;
      bms[addr].rem_ah = u16(b, p) * 0.01; p += 3; 
      bms[addr].full_ah = u16(b, p) * 0.01; p += 2;

      if(bms[addr].full_ah > 0) bms[addr].soc = (int)((bms[addr].rem_ah / bms[addr].full_ah) * 100.0);
      else bms[addr].soc = 0; 
      if(bms[addr].soc > 100) bms[addr].soc = 100;

      if(p+1 < blen) { p+=2; } 
      if(p+1 < blen) { bms[addr].soh = u16(b, p); p+=2; }
      if(bms[addr].soh > 100) bms[addr].soh = 100;
  }

  bms[addr].valid = true;
  bms[addr].last_seen = millis();
}

// =====================================================================
// AGGREGATION & LOGIC
// =====================================================================
void calculateVictronData() {
  float sumI=0, sumV=0, sumSOC=0, sumSOH=0, sumT=0, sumCap=0;
  int count=0;
  
  for(int i=0; i<g_bms_count; i++) {
    bool is_online = bms[i].valid && (millis() - bms[i].last_seen < 60000);
    if(simulation_active) is_online = true;

    if(is_online) {
       sumI += bms[i].current;
       sumV += bms[i].voltage;
       sumSOC += bms[i].soc;
       sumSOH += bms[i].soh;
       sumT += bms[i].maxTemp;
       sumCap += bms[i].full_ah;
       count++;
    }
  }

  if(count > 0) {
      victronData.activePacks = count;
      victronData.totalCurrent = sumI;
      victronData.avgVoltage = sumV / count;
      victronData.totalPower = sumI * victronData.avgVoltage;
      victronData.avgSOC = sumSOC / count;
      victronData.avgSOH = sumSOH / count;
      victronData.avgTemp = sumT / count;
      victronData.totalCapacity = sumCap;
      
      victronData.maxChargeCurrent = count * g_charge_amps;
      victronData.maxDischargeCurrent = count * g_discharge_amps;
      
      if(victronData.avgSOC >= 99) victronData.maxChargeCurrent = 5.0; 
      if(victronData.avgSOC == 100) victronData.maxChargeCurrent = 0.0;
  } else {
      victronData.activePacks = 0;
      victronData.totalCurrent = 0;
      victronData.totalPower = 0;
      victronData.totalCapacity = 0;
  }
}

void calculateEnergy() {
    unsigned long now = millis();
    if(last_energy_calc == 0) { last_energy_calc = now; return; }
    double hours = (now - last_energy_calc) / 3600000.0;
    last_energy_calc = now;
    double power_kw = victronData.totalPower / 1000.0;
    if(power_kw > 0) energy_charged_kwh += (power_kw * hours);
    else energy_discharged_kwh += ((-power_kw) * hours);
    
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        if(current_day != timeinfo.tm_mday) {
            current_day = timeinfo.tm_mday;
            energy_charged_kwh = 0; energy_discharged_kwh = 0;
            addToLog("Neuer Tag: Energiezähler Reset", false);
        }
    }
}

void updateHistory() {
    powerHistory[historyIdx] = (int16_t)victronData.totalPower;
    historyIdx++;
    if(historyIdx >= HISTORY_LEN) historyIdx = 0;
}

// =====================================================================
// FEATURES: SD, MQTT, CAN
// =====================================================================
void initSD() {
    if(!g_sd_enable || BOARD_TYPE != 1) return; 
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if(!SD.begin(SD_CS)) {
        addToLog("SD Init Fehler!", true);
        sd_ok = false;
    } else {
        addToLog("SD Karte OK", false);
        sd_ok = true;
    }
}

void writeLogToSD() {
    if(!g_sd_enable || !sd_ok) return;
    File f = SD.open("/log.csv", FILE_APPEND);
    if(f) {
        String line = getTimeStr() + ";" + String(victronData.avgSOC,1) + ";" + String(victronData.totalPower,0) + ";" + String(victronData.totalCurrent,1);
        f.println(line);
        f.close();
    } else { sd_ok = false; }
}

void handleSDDownload() {
    if(!sd_ok) { server.send(404, "text/plain", "SD not ready"); return; }
    File f = SD.open("/log.csv", FILE_READ);
    if(f) { server.streamFile(f, "text/csv"); f.close(); } 
    else server.send(404, "text/plain", "No Log");
}

void handleSDClear() {
    if(sd_ok && SD.exists("/log.csv")) { SD.remove("/log.csv"); server.send(200, "text/plain", "Deleted"); }
    else server.send(404, "text/plain", "Error");
}

void mqttReconnect() {
  if (!g_mqtt_enable || g_mqtt_server == "") return;
  if (!mqtt.connected()) {
    if (mqtt.connect("VictronGateway", g_mqtt_user.c_str(), g_mqtt_pass.c_str())) {
      addToLog("MQTT Verbunden", false);
      mqtt.publish("victron/gateway/status", "online");
    }
  }
}

void sendMqttData() {
    if(!g_mqtt_enable || !mqtt.connected()) return;
    String json = "{\"soc\":" + String(victronData.avgSOC, 1) + ",\"voltage\":" + String(victronData.avgVoltage, 2) + ",\"current\":" + String(victronData.totalCurrent, 1) + ",\"power\":" + String(victronData.totalPower, 0) + "}";
    mqtt.publish("victron/system", json.c_str());
}

void sendCanFrame(uint32_t id, uint8_t* data) {
    twai_message_t msg;
    msg.identifier = id; msg.extd=0; msg.data_length_code=8;
    memcpy(msg.data, data, 8);
    esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(10));
    stat_tx_count++;
    
    if (res == ESP_OK) {
        setLed(0, 5, 0); 
        can_error_flag = false;
        debug_can_status = "CAN OK";
    } else {
        can_error_flag = true;
        debug_can_status = "TX ERR " + String(res);
        setLed(50, 0, 0); 
    }
}

void sendVictronCAN() {
    uint8_t d[8] = {0};
    float cvl_val = g_expert_mode ? g_cvl_voltage : (g_force_cell_count * 3.50); 
    int cv = (int)(cvl_val * 10);
    int ccl = (int)(victronData.maxChargeCurrent * 10);
    int dcl = (int)(victronData.maxDischargeCurrent * 10);
    d[0]=cv&0xFF; d[1]=cv>>8; d[2]=ccl&0xFF; d[3]=ccl>>8; d[4]=dcl&0xFF; d[5]=dcl>>8;
    sendCanFrame(0x351, d); delay(2);

    d[0]=((int)victronData.avgSOC)&0xFF; d[1]=((int)victronData.avgSOC)>>8;
    d[2]=((int)victronData.avgSOH)&0xFF; d[3]=((int)victronData.avgSOH)>>8;
    int cap = (int)(victronData.totalCapacity * 10); 
    d[4]=cap&0xFF; d[5]=cap>>8; d[6]=0; d[7]=0;
    sendCanFrame(0x355, d); delay(2);

    int v = (int)(victronData.avgVoltage * 100);
    int i = (int)(victronData.totalCurrent * 10);
    int t = (int)(victronData.avgTemp * 10);
    d[0]=v&0xFF; d[1]=v>>8; d[2]=i&0xFF; d[3]=i>>8; d[4]=t&0xFF; d[5]=t>>8; d[6]=0; d[7]=0;
    sendCanFrame(0x356, d); delay(2);
    
    memset(d,0,8); sendCanFrame(0x359, d);
    char n[]="LILYGO"; sendCanFrame(0x35E, (uint8_t*)n);
    debug_can_status = "Sending OK (" + String(victronData.activePacks) + " Packs)";
}

// =====================================================================
// WEB UI & OTA
// =====================================================================
void handleRoot() {
  // SPLIT HTML TO AVOID COMPILER ERROR
  // PART 1
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="de" id="htmlRoot">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Topband-Victron Gateway</title>
  <style>
    :root { --bg-color: #121212; --card-bg: #1e1e1e; --text-color: #e0e0e0; --border-color: #333; --input-bg: #333; --accent: #2979ff; }
    .light-mode { --bg-color: #f5f5f5; --card-bg: #ffffff; --text-color: #333333; --border-color: #cccccc; --input-bg: #eeeeee; --accent: #1565c0; }
    body { font-family: 'Segoe UI', sans-serif; background-color: var(--bg-color); color: var(--text-color); margin: 0; padding: 10px; transition: background 0.3s; }
    h1 { text-align: center; margin: 10px 0; font-size:1.4rem; }
    .top-controls { display:flex; justify-content:center; gap:10px; margin-bottom:15px; }
    .toggle-btn { background: var(--input-bg); color: var(--text-color); border: 1px solid var(--border-color); padding: 6px 12px; border-radius: 20px; cursor: pointer; }
    .dashboard { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 15px; }
    .bms-card { background-color: var(--card-bg); border-radius: 12px; padding: 15px; border: 1px solid var(--border-color); }
    .main-stats { display: flex; justify-content: space-around; background: rgba(128,128,128,0.1); padding: 8px; border-radius: 8px; margin-bottom: 10px; }
    .stat-box { text-align: center; } .stat-value { font-weight: bold; display: block; } .stat-label { font-size: 0.7rem; opacity:0.7; }
    .c-blue { color: #2979ff; } .c-cyan { color: #4dabf7; } .c-red { color: #ff6b6b; } .c-orange { color: #ffab40; } .c-green { color: #4f4; }
    .light-mode .c-cyan { color: #0277bd; } .light-mode .c-red { color: #c62828; } .light-mode .c-orange { color: #ef6c00; } .light-mode .c-green { color: #2e7d32; }
    .state-idle { border-left: 5px solid #3949ab; } .state-charge { border-left: 5px solid #2e7d32; } .state-discharge { border-left: 5px solid #ef6c00; }
    .can-status-box { text-align:center; padding:10px; border-radius:8px; margin-bottom:20px; font-weight:bold; }
    .can-ok { background: #2e7d32; color: #fff; } .can-err { background: #c62828; color: #fff; animation: blink 2s infinite; }
    @keyframes blink { 0% {opacity:1} 50% {opacity:0.7} 100% {opacity:1} }
    .pack-voltage-container { text-align: center; margin: 10px 0; }
    .pack-voltage-value { font-size: 2rem; font-weight: bold; color: var(--accent); }
    .cell-stats-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4px; background: rgba(128,128,128,0.1); padding: 8px; border-radius: 8px; text-align: center; margin-bottom: 10px; }
    .cs-val { font-weight: bold; } .cs-lbl { font-size: 0.7rem; opacity:0.7; }
    .cells-wrapper { margin-top: 15px; border-top: 1px solid var(--border-color); padding-top: 10px; }
    .cells-title { font-size: 0.9em; color: #aaa; margin-bottom: 8px; }
    .cell-grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 4px; }
    .cell-box { background: rgba(128,128,128,0.1); padding: 4px; border-radius: 3px; text-align: center; font-size: 0.75em; border: 1px solid transparent; }
    .cell-num { font-size: 0.7em; color: #888; display:block; } .cell-val { font-weight: bold; }
    .cell-min { border: 1px solid #4dabf7; background: rgba(77, 171, 247, 0.15); }
    .cell-min .cell-val { color: #4dabf7; }
    .cell-max { border: 1px solid #ff6b6b; background: rgba(255, 107, 107, 0.15); }
    .cell-max .cell-val { color: #ff6b6b; }
    .temp-grid { display: flex; flex-wrap: wrap; gap: 5px; font-size: 0.8rem; color: #ccc; margin-top:10px; }
    .temp-item { background: rgba(128,128,128,0.2); padding: 2px 6px; border-radius: 4px; }
    details.bms-list-item { background: var(--card-bg); border: 1px solid var(--border-color); border-radius: 8px; margin-bottom: 8px; }
    details.bms-list-item summary { padding: 10px 15px; cursor: pointer; font-weight: bold; outline: none; list-style: none; display: flex; align-items: center; background: rgba(128,128,128,0.1); font-family: monospace; }
    details.bms-list-item summary::-webkit-details-marker { display: none; }
    details.bms-list-item[open] summary { background: #2a2a2a; border-bottom: 1px solid #333; }
    .list-row { display: grid; grid-template-columns: 80px 1fr 1fr 1fr 1fr 20px; width: 100%; align-items: center; gap:5px; }
    .list-id { color: #fff; font-weight:bold; } .list-val { text-align: right; white-space:nowrap; }
    .list-arrow { text-align: right; color: #666; transition: transform 0.2s; }
    details.bms-list-item[open] .list-arrow { transform: rotate(180deg); }
    .bms-content-inner { padding: 15px; }
    .card-header-stats { display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-color); padding-bottom: 10px; margin-bottom: 10px; font-size:0.9em;}
    .chs-item { text-align:center; } .chs-lbl { font-size:0.7em; color:#888; display:block; }
    .victron-card { background: #1a237e; border-color: #3949ab; }
    .logbox { background: #000; color: #0f0; font-family: monospace; font-size: 0.7rem; height: 100px; overflow-y: scroll; padding: 5px; border: 1px solid #444; border-radius: 5px; margin-bottom: 10px; }
    .log-err { color: #f44; } .log-ok { color: #4f4; }
    details.settings-box { background: var(--card-bg); padding: 10px; border-radius: 8px; margin-top: 30px; border: 1px solid var(--border-color); }
    .settings-form { display: grid; gap: 10px; margin-top: 10px; max-width: 400px; }
    .input-group { display: flex; justify-content: space-between; align-items: center; }
    input { padding: 5px; background: var(--input-bg); color: var(--text-color); border: 1px solid var(--border-color); border-radius: 4px; }
    button.save-btn { background: #2e7d32; color: white; border: none; padding: 8px; border-radius: 4px; cursor: pointer; width: 100%; font-weight: bold; margin-top:10px;}
    .legend { font-size: 0.8em; margin-top: 20px; color: #888; border-top: 1px solid #333; padding-top: 10px; display:flex; gap:15px; flex-wrap:wrap;}
    .dot { display:inline-block; width:10px; height:10px; border-radius:50%; margin-right:5px; }
    .footer { text-align: center; font-size: 0.7em; color: #555; margin-top: 30px; }
    .expert-warning { color: #f44; font-size: 0.8em; margin-top:5px; display:none; }
    .chart-container { width: 100%; height: 200px; background: rgba(0,0,0,0.2); border: 1px solid var(--border-color); border-radius: 8px; margin-top: 15px; }
    canvas { width: 100%; height: 100%; display:block; }
  </style>
</head>
<body>
  <div class="top-bar">
      <h1>Topband Gateway</h1>
      <div class="btn-group" style="display:flex; gap:10px;">
          <button class="toggle-btn" id="langBtn" onclick="toggleLang()">DE</button>
          <button class="toggle-btn" id="themeBtn" onclick="toggleTheme()">🌗</button>
          <button class="toggle-btn" onclick="toggleView()" id="viewBtn">LIST</button>
      </div>
  </div>
  <div style="text-align:center; margin-bottom:10px;"><a href="/update" style="font-size:0.8em; color:var(--accent);">Firmware Update</a></div>
  <div id="statusBox" class="can-status-box can-ok"><span class="txt-init">Initialisiere...</span></div>
  <div class="dashboard" style="margin-bottom: 20px;">
     <div id="mainCard" class="bms-card" style="background:#1a237e; color:white; border:none;">
        <div style="padding:10px; border-bottom:1px solid rgba(255,255,255,0.1); font-weight:bold; display:flex; justify-content:space-between;">
            <span class="txt-sys">Gesamtsystem</span>
            <span id="sysStatus" style="font-size:0.8em; opacity:0.8">Standby</span>
        </div>
        <div class="main-stats" style="background: rgba(0,0,0,0.2);">
           <div class="stat-box"><span class="stat-value" id="total-curr">-- A</span><span class="stat-label txt-curr">Strom</span></div>
           <div class="stat-box"><span class="stat-value" id="total-power">-- W</span><span class="stat-label txt-pwr">Leistung</span></div>
           <div class="stat-box"><span class="stat-value" id="avg-soc">-- %</span><span class="stat-label">SOC</span></div>
           <div class="stat-box"><span class="stat-value" id="total-cap">-- Ah</span><span class="stat-label txt-cap">Kapazität</span></div>
        </div>
  )rawliteral";
  
  // PART 2
  html += R"rawliteral(
        <div style="padding:10px; border-top:1px solid rgba(255,255,255,0.1); font-size:0.9em; display:flex; justify-content:space-around;">
             <span class="txt-in">Geladen: <span id="e_in" class="c-green">--</span> kWh</span>
             <span class="txt-out">Entladen: <span id="e_out" class="c-red">--</span> kWh</span>
        </div>
        <div style="padding:10px;">
            <div style="font-size:0.8em; opacity:0.7; margin-bottom:5px;" class="txt-hist">Leistungsverlauf (24h)</div>
            <div class="chart-container"><canvas id="powerChart"></canvas></div>
        </div>
     </div>
  </div>

  <div id="container" class="dashboard"></div>

  <div class="bms-card" style="margin-top:20px;">
    <div style="padding:10px; border-bottom:1px solid var(--border-color); font-weight:bold; display:flex; justify-content:space-between;">
        <span class="txt-diag">Diagnose / Log</span>
        <span style="font-size:0.8em; font-weight:normal; opacity:0.7;">Rx: <span id="stat_rx">0</span> / Tx: <span id="stat_tx">0</span></span>
    </div>
    <div id="logbox" class="logbox">Warte auf Daten...</div>
  </div>

  <div class="legend">
    <span><span class="dot" style="background:#1b5e20"></span><span class="txt-chg">Laden</span></span>
    <span><span class="dot" style="background:#e65100"></span><span class="txt-dis">Entladen</span></span>
    <span><span class="dot" style="background:#4dabf7"></span>Min</span>
    <span><span class="dot" style="background:#ff6b6b"></span>Max</span>
  </div>
  
  <details class="settings-box">
    <summary>💾 SD-Logging</summary>
    <div style="padding:10px;">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
            <a href="/sd/download" target="_blank" class="toggle-btn" style="text-decoration:none;">Download</a>
            <a href="/sd/clear" target="_blank" class="toggle-btn" style="background:#800; border-color:#a00;">Clear</a>
        </div>
        <form action="/save" method="POST">
            <div class="input-group" style="margin-top:10px;">
                <label>SD-Logging aktivieren:</label>
  )rawliteral";
  
  html += "<input type='checkbox' name='sd_en' " + String(g_sd_enable ? "checked" : "") + ">";
  
  // PART 3
  html += R"rawliteral(
            </div>
            <button type="submit" class="save-btn txt-save">Speichern</button>
        </form>
    </div>
  </details>

  <details class="settings-box">
    <summary class="txt-set">⚙ Einstellungen</summary>
    <form class="settings-form" action="/save" method="POST">
      <div class="input-group"><label class="txt-cnt">Anzahl BMS:</label><input type="number" name="count" value=")rawliteral" + String(g_bms_count) + R"rawliteral(" min="1" max="16"></div>
      <div class="input-group"><label class="txt-cel">Zellen (15/16):</label><input type="number" name="cells" value=")rawliteral" + String(g_force_cell_count) + R"rawliteral(" min="10" max="16"></div>
      <div class="input-group"><label class="txt-lchg">Laden (A):</label><input type="number" name="charge" value=")rawliteral" + String(g_charge_amps, 1) + R"rawliteral("></div>
      <div class="input-group"><label class="txt-ldis">Entladen (A):</label><input type="number" name="discharge" value=")rawliteral" + String(g_discharge_amps, 1) + R"rawliteral("></div>
      <div class="input-group" style="margin-top:10px;"><label class="txt-exp">⚠️ Expert:</label>
  )rawliteral";
  
  html += "<input type='checkbox' name='expert' id='expertCheck' onclick='toggleExpert()' " + String(g_expert_mode ? "checked" : "") + ">";
  
  // PART 4
  html += R"rawliteral(
      </div>
      <div class="input-group" id="cvlGroup" style="opacity:0.5; pointer-events:none;"><label>CVL (V):</label><input type="number" name="cvl" id="cvlInput" value=")rawliteral" + String(g_cvl_voltage, 1) + R"rawliteral(" step="0.1"></div>
      <button type="submit" class="save-btn txt-save">Speichern</button>
    </form>
  </details>
  
  <details class="settings-box">
    <summary>📡 MQTT</summary>
    <form class="settings-form" action="/save" method="POST">
      <div class="input-group"><label class="txt-act">Aktivieren:</label>
  )rawliteral";
  
  html += "<input type='checkbox' name='mqtt_en' " + String(g_mqtt_enable ? "checked" : "") + ">";

  // PART 5 (SCRIPTS)
  html += R"rawliteral(
      </div>
      <div class="input-group"><label>IP:</label><input type="text" name="mqtt_ip" value=")rawliteral" + g_mqtt_server + R"rawliteral("></div>
      <div class="input-group"><label>Port:</label><input type="number" name="mqtt_port" value=")rawliteral" + String(g_mqtt_port) + R"rawliteral("></div>
      <div class="input-group"><label>User:</label><input type="text" name="mqtt_user" value=")rawliteral" + g_mqtt_user + R"rawliteral("></div>
      <div class="input-group"><label>Pass:</label><input type="text" name="mqtt_pass" value=")rawliteral" + g_mqtt_pass + R"rawliteral("></div>
      <button type="submit" class="save-btn txt-save">Speichern</button>
    </form>
  </details>

  <div class="footer">
    <p>Topband Gateway V72 Ultimate</p>
  </div>

  <script>
    const TR = {
        de: { sys: "Gesamtsystem", curr: "Strom", pwr: "Leistung", cap: "Kapazität", in: "Geladen:", out: "Entladen:", hist: "Leistungsverlauf (24h)", diag: "Diagnose / Log", chg: "Laden", dis: "Entladen", set: "⚙ Einstellungen", cnt: "Anzahl BMS:", cel: "Zellen:", lchg: "Laden (A):", ldis: "Entladen (A):", exp: "⚠️ Experten-Modus:", save: "Speichern", act: "Aktivieren:", init: "Initialisiere...", stby: "Standby", no_conn: "Keine Verbindung" },
        en: { sys: "System Total", curr: "Current", pwr: "Power", cap: "Capacity", in: "Charged:", out: "Discharged:", hist: "Power History (24h)", diag: "Diagnostics / Log", chg: "Charging", dis: "Discharging", set: "⚙ Settings", cnt: "BMS Count:", cel: "Cells:", lchg: "Charge (A):", ldis: "Discharge (A):", exp: "⚠️ Expert Mode:", save: "Save", act: "Enable:", init: "Initializing...", stby: "Standby", no_conn: "No Connection" }
    };
    let lang = localStorage.getItem('lang') || 'de';
    let theme = localStorage.getItem('theme') || 'dark';
    let viewMode = localStorage.getItem('viewMode') || 'list';

    function applyLang() {
        const t = TR[lang];
        document.getElementById('langBtn').innerText = lang.toUpperCase();
        document.querySelector('.txt-sys').innerText = t.sys;
        document.querySelector('.txt-curr').innerText = t.curr;
        document.querySelector('.txt-pwr').innerText = t.pwr;
        document.querySelector('.txt-cap').innerText = t.cap;
        document.querySelector('.txt-in').childNodes[0].nodeValue = t.in + " ";
        document.querySelector('.txt-out').childNodes[0].nodeValue = t.out + " ";
        document.querySelector('.txt-hist').innerText = t.hist;
        document.querySelector('.txt-diag').innerText = t.diag;
        document.querySelector('.txt-chg').innerText = t.chg;
        document.querySelector('.txt-dis').innerText = t.dis;
        document.querySelector('.txt-set').innerText = t.set;
        document.querySelector('.txt-cnt').innerText = t.cnt;
        document.querySelector('.txt-cel').innerText = t.cel;
        document.querySelector('.txt-lchg').innerText = t.lchg;
        document.querySelector('.txt-ldis').innerText = t.ldis;
        document.querySelector('.txt-exp').innerText = t.exp;
        document.querySelectorAll('.txt-save').forEach(b => b.innerText = t.save);
    }
    
    function toggleLang() { lang = (lang === 'de') ? 'en' : 'de'; localStorage.setItem('lang', lang); applyLang(); }
    function applyTheme() { const h = document.getElementById('htmlRoot'); if(theme === 'light') h.classList.add('light-mode'); else h.classList.remove('light-mode'); }
    function toggleTheme() { theme = (theme === 'dark') ? 'light' : 'dark'; localStorage.setItem('theme', theme); applyTheme(); }
    function toggleExpert() { const c = document.getElementById('expertCheck'); const g = document.getElementById('cvlGroup'); if(c.checked) { g.style.opacity='1'; g.style.pointerEvents='auto'; } else { g.style.opacity='0.5'; g.style.pointerEvents='none'; } }
    function toggleView() { viewMode = (viewMode === 'cards') ? 'list' : 'cards'; localStorage.setItem('viewMode', viewMode); document.getElementById('viewBtn').innerText = (viewMode==='cards')?'CARDS':'LIST'; updateDashboard(); }
    
    applyLang(); applyTheme(); toggleExpert(); document.getElementById('viewBtn').innerText = (viewMode==='cards')?'CARDS':'LIST';

    const cvs = document.getElementById('powerChart');
    const ctx = cvs.getContext('2d');
    function drawChart(arr) {
        cvs.width = cvs.clientWidth; cvs.height = cvs.clientHeight;
        const w = cvs.width; const h = cvs.height;
        ctx.clearRect(0,0,w,h);
        let max=100; let min=-100;
        arr.forEach(v=>{if(v>max)max=v;if(v<min)min=v;});
        max*=1.1; min*=1.1; const range=max-min; const zY=h-((0-min)/range*h);
        ctx.strokeStyle=(theme==='light'?'#ccc':'#555'); ctx.beginPath(); ctx.moveTo(0,zY); ctx.lineTo(w,zY); ctx.stroke();
        if(arr.length<2)return;
        const sX=w/(arr.length-1);
        ctx.beginPath(); ctx.moveTo(0,zY);
        let x=0;
        for(let i=0;i<arr.length;i++){ ctx.lineTo(x, h-((arr[i]-min)/range*h)); x+=sX; }
        ctx.lineTo(w,zY); ctx.closePath();
        ctx.fillStyle='rgba(41,121,255,0.3)'; ctx.fill();
        ctx.strokeStyle='#2979ff'; ctx.lineWidth=2; ctx.stroke();
    }

    function getSocColor(s) { if(s>=50)return'#4f4'; if(s>=20)return'#fb0'; return'#f44'; }
    function getTempLabel(i,c) { if(c>=6){ if(i<4)return"Cell "+(i+1); if(i===4)return"Bal"; if(i===5)return"MOS"; if(i===6)return"Env"; } return"T"+(i+1); }

    function genContent(bms) {
        let th=''; if(bms.temps) bms.temps.forEach((t,i)=>{th+=`<div class="temp-item">${getTempLabel(i,bms.temps.length)}: ${t.toFixed(1)}°C</div>`;});
        let ch=''; if(bms.cells) bms.cells.forEach((v,i)=>{ let c='cell-box'; if(i+1===bms.min_idx)c+=' cell-min'; if(i+1===bms.max_idx)c+=' cell-max'; ch+=`<div class="${c}"><span class="cell-num">${i+1}</span><span class="cell-val">${v.toFixed(3)}</span></div>`;});
        return `<div class="pack-voltage-container"><div class="pack-voltage-value">${bms.pack_v.toFixed(2)} V</div><div style="font-size:0.8em; opacity:0.7;">${bms.rem_ah.toFixed(1)} / ${bms.full_ah.toFixed(1)} Ah</div></div>
        <div class="main-stats"><div class="stat-box"><span class="stat-value">${bms.current.toFixed(1)} A</span><span class="stat-label">A</span></div><div class="stat-box"><span class="stat-value" style="color:${getSocColor(bms.soc)}">${bms.soc.toFixed(0)} %</span><span class="stat-label">SOC</span></div><div class="stat-box"><span class="stat-value">${bms.soh.toFixed(0)} %</span><span class="stat-label">SOH</span></div></div>
        <div class="cell-stats-grid"><div><div class="cs-val c-cyan">${bms.min_cell.toFixed(3)} V</div><div class="cs-lbl">Min</div></div><div><div class="cs-val">${bms.avg_cell.toFixed(3)} V</div><div class="cs-lbl">Ø</div></div><div><div class="cs-val c-red">${bms.max_cell.toFixed(3)} V</div><div class="cs-lbl">Max</div></div></div>
        <div class="cells-wrapper"><div class="cells-title">Cells</div><div class="cell-grid">${ch}</div></div><div class="temp-grid">${th}</div>`;
    }

    function updateDashboard() {
      const openDetails={}; document.querySelectorAll('details.bms-list-item').forEach(el=>{if(el.hasAttribute('open'))openDetails[el.getAttribute('id')]=true;});
      fetch('/data').then(r=>r.json()).then(d=>{
          document.getElementById('total-curr').innerText=d.victron.total_amps.toFixed(1)+" A";
          document.getElementById('avg-soc').innerText=d.victron.avg_soc.toFixed(1)+" %";
          document.getElementById('total-cap').innerText=d.victron.total_cap.toFixed(1)+" Ah";
          document.getElementById('total-power').innerText=d.victron.total_power.toFixed(0)+" W";
          document.getElementById('logbox').innerHTML=d.log;
          document.getElementById('stat_rx').innerText=d.stat_rx; document.getElementById('stat_tx').innerText=d.stat_tx;
          document.getElementById('e_in').innerText=d.energy.in.toFixed(2); document.getElementById('e_out').innerText=d.energy.out.toFixed(2);
          
          if(d.history) { let o=[]; let x=d.hist_idx||0; for(let i=0;i<d.history.length;i++)o.push(d.history[(x+i)%d.history.length]); drawChart(o); }

          const sb=document.getElementById('statusBox');
          if(d.can_error){sb.className="can-status-box can-err";sb.innerText="CAN ERROR";}else{sb.className="can-status-box can-ok";sb.innerText="CAN OK ("+d.can_status+")";}
          
          const mc=document.getElementById('mainCard'); const ss=document.getElementById('sysStatus');
          let c=d.victron.total_amps;
          if(c>0.5){mc.className="bms-card sys-card state-charge"; ss.innerText=(lang==='de'?"Laden":"Charging");}
          else if(c<-0.5){mc.className="bms-card sys-card state-discharge"; ss.innerText=(lang==='de'?"Entladen":"Discharging");}
          else{mc.className="bms-card sys-card state-idle"; ss.innerText=(lang==='de'?"Standby":"Idle");}

          let h='';
          d.bms.forEach((b,i)=>{
              const id=`bms-detail-${i}`;
              const t=TR[lang];
              const ic=b.online?genContent(b):`<div style="text-align:center;padding:20px;">${t.no_conn}</div>`;
              let sc=getSocColor(b.soc); let dt=(b.max_cell-b.min_cell).toFixed(3); let at=b.avg_temp?b.avg_temp.toFixed(1):'-';
              let st=""; if(b.online){if(b.current>0.5)st="state-charge";else if(b.current<-0.5)st="state-discharge";else st="state-idle";}

              if(viewMode==='list'){
                  if(!b.online){h+=`<details id="${id}" class="bms-list-item" style="opacity:0.5"><summary><div class="list-row"><span class="list-id">BMS ${i+1}</span><span style="color:#f44">OFFLINE</span><span>-</span><span>-</span><span>-</span></div></summary></details>`;}
                  else{h+=`<details id="${id}" class="bms-list-item ${st}"><summary><div class="list-row"><span class="list-id">BMS ${i+1}</span><span style="color:${sc}">${b.soc}%</span><span class="c-blue">${b.pack_v.toFixed(2)}V</span><span class="c-red">∆${dt}V</span><span class="c-orange">${at}°C</span><span class="list-arrow">▼</span></div></summary><div class="bms-content-inner">${ic}</div></details>`;}
              } else {
                  if(!b.online){h+=`<div class="bms-card" style="opacity:0.6"><div class="bms-header"><span class="bms-title">BMS #${i+1}</span><span class="status-badge offline">OFFLINE</span></div>${ic}</div>`;}
                  else{h+=`<div class="bms-card ${st}"><div class="bms-header"><span class="bms-title">BMS #${i+1}</span><span class="status-badge online">ONLINE</span></div><div class="card-header-stats"><div class="chs-item"><span class="chs-lbl">Drift</span><span class="c-red">∆${dt}V</span></div><div class="chs-item"><span class="chs-lbl">Temp</span><span class="c-orange">${at}°C</span></div></div>${ic}</div>`;}
              }
          });
          document.getElementById('container').innerHTML=h;
          Object.keys(openDetails).forEach(id=>{const el=document.getElementById(id); if(el)el.setAttribute('open','');});
      });
    }
    setInterval(updateDashboard, 2000); updateDashboard();
  </script>
</body>
</html>
  )rawliteral";
  
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{ \"victron\": {";
  json += "\"active\": " + String(victronData.activePacks > 0 ? "true" : "false") + ",";
  json += "\"packs\": " + String(victronData.activePacks) + ",";
  json += "\"total_amps\": " + String(victronData.totalCurrent, 1) + ",";
  json += "\"total_power\": " + String(victronData.totalPower, 0) + ","; 
  json += "\"avg_soc\": " + String(victronData.avgSOC, 1) + ",";
  json += "\"total_cap\": " + String(victronData.totalCapacity, 1) + ",";
  json += "\"ccl\": " + String(victronData.maxChargeCurrent, 1);
  json += "}, ";
  json += "\"log\": \"" + debug_log + "\", ";
  json += "\"can_status\": \"" + debug_can_status + "\", ";
  json += "\"can_error\": " + String(can_error_flag ? "true" : "false") + ", ";
  json += "\"stat_rx\": " + String(stat_rx_count) + ", ";
  json += "\"stat_tx\": " + String(stat_tx_count) + ", ";
  
  json += "\"energy\": {\"in\": " + String(energy_charged_kwh, 2) + ", \"out\": " + String(energy_discharged_kwh, 2) + "}, ";
  
  json += "\"hist_idx\": " + String(historyIdx) + ", \"history\": [";
  for(int i=0; i<HISTORY_LEN; i++) {
      if(i>0) json += ",";
      json += String(powerHistory[i]);
  }
  json += "], ";

  json += "\"bms\": [";
  for (int i = 0; i < g_bms_count; i++) { 
    if (i > 0) json += ",";
    bool online = bms[i].valid && (millis() - bms[i].last_seen < 60000) || simulation_active;
    
    json += "{ \"id\":" + String(i) + ", \"online\":" + String(online ? "true" : "false");
    if (online) {
      json += ", \"pack_v\":" + String(bms[i].voltage, 2);
      json += ", \"current\":" + String(bms[i].current, 2);
      json += ", \"soc\":" + String(bms[i].soc);
      json += ", \"soh\":" + String(bms[i].soh);
      json += ", \"rem_ah\":" + String(bms[i].rem_ah, 1);
      json += ", \"full_ah\":" + String(bms[i].full_ah, 1);
      json += ", \"min_cell\":" + String(bms[i].minCellV, 3);
      json += ", \"max_cell\":" + String(bms[i].maxCellV, 3);
      json += ", \"avg_cell\":" + String(bms[i].avgCellV, 3);
      json += ", \"min_idx\":" + String(bms[i].minCellIdx);
      json += ", \"max_idx\":" + String(bms[i].maxCellIdx);
      json += ", \"avg_temp\":" + String(bms[i].avgTemp, 1);
      json += ", \"temps\":[";
      for(int t=0; t<bms[i].temp_count; t++) { if(t>0) json += ","; json += String(bms[i].temps[t], 1); }
      json += "]";
      json += ", \"cells\":[";
      for(int c=0; c<bms[i].cell_count; c++) { if(c>0) json += ","; json += String(bms[i].cells[c], 3); }
      json += "]";
    }
    json += "}";
  }
  json += "] }";
  server.send(200, "application/json", json);
}

void handleSim() {
  if (server.hasArg("act")) simulation_active = (server.arg("act") == "1");
  if(simulation_active) {
      for(int i=0; i<g_bms_count; i++) {
          bms[i].valid = true; bms[i].voltage=53.0; bms[i].current=10; bms[i].soc=80; bms[i].soh=100;
          bms[i].rem_ah=224; bms[i].full_ah=280; 
          bms[i].temp_count=7; 
          for(int k=0;k<7;k++) bms[i].temps[k]=25.0+k; 
          bms[i].minCellV=3.3; bms[i].maxCellV=3.4; bms[i].avgCellV=3.35;
          bms[i].cell_count = g_force_cell_count;
          for(int c=0;c<g_force_cell_count;c++) bms[i].cells[c]=3.30 + (c*0.01);
          bms[i].minCellIdx = 1; bms[i].maxCellIdx = g_force_cell_count;
          bms[i].last_seen = millis();
          bms[i].avgTemp = 28.5;
      }
      addToLog("Simulation gestartet", false);
  }
  server.sendHeader("Location", "/"); server.send(303);
}

void handleSave() {
  if (server.hasArg("count")) g_bms_count = server.arg("count").toInt();
  if (server.hasArg("cells")) g_force_cell_count = server.arg("cells").toInt();
  if (server.hasArg("charge")) g_charge_amps = server.arg("charge").toFloat();
  if (server.hasArg("discharge")) g_discharge_amps = server.arg("discharge").toFloat();
  
  if (server.hasArg("expert")) g_expert_mode = true; else g_expert_mode = false;
  if (server.hasArg("cvl")) {
      float v = server.arg("cvl").toFloat();
      if(v >= 50 && v <= 58) g_cvl_voltage = v; 
  }
  
  if (server.hasArg("sd_en")) g_sd_enable = true; else g_sd_enable = false;
  
  if (server.hasArg("mqtt_en")) g_mqtt_enable = true; else g_mqtt_enable = false;
  if (server.hasArg("mqtt_ip")) g_mqtt_server = server.arg("mqtt_ip");
  if (server.hasArg("mqtt_port")) g_mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("mqtt_user")) g_mqtt_user = server.arg("mqtt_user");
  if (server.hasArg("mqtt_pass")) g_mqtt_pass = server.arg("mqtt_pass");

  preferences.putInt("cnt", g_bms_count);
  preferences.putInt("cells", g_force_cell_count);
  preferences.putFloat("chg", g_charge_amps);
  preferences.putFloat("dis", g_discharge_amps);
  preferences.putBool("exp", g_expert_mode);
  preferences.putFloat("cvl", g_cvl_voltage);
  
  preferences.putBool("sd_en", g_sd_enable);
  
  preferences.putBool("mq_en", g_mqtt_enable);
  preferences.putString("mq_ip", g_mqtt_server);
  preferences.putInt("mq_pt", g_mqtt_port);
  preferences.putString("mq_us", g_mqtt_user);
  preferences.putString("mq_pw", g_mqtt_pass);

  server.send(200, "text/html", "<h1>Gespeichert! Starte neu...</h1><script>setTimeout(function(){window.location.href='/';}, 3000);</script>");
  delay(1000); ESP.restart();
}

void handleUpdate() { server.send(200, "text/html", "<form method='POST' action='/update2' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>"); }
void handleUpdate2() { server.sendHeader("Connection", "close"); server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); ESP.restart(); }
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) { if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); }
  else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial); }
  else if (upload.status == UPLOAD_FILE_END) { if (Update.end(true)) Serial.printf("Update Success: %u\n", upload.totalSize); else Update.printError(Serial); }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Topband Victron Gateway V72 Ultimate Fixed ===");
  
  #if BOARD_TYPE == 2
    pinMode(PIN_RS485_DIR, OUTPUT); digitalWrite(PIN_RS485_DIR, LOW);
  #else
    pinMode(PIN_PWR_BOOST, OUTPUT); digitalWrite(PIN_PWR_BOOST, HIGH);
    pinMode(PIN_485_EN, OUTPUT); digitalWrite(PIN_485_EN, HIGH);
    pinMode(PIN_485_CB, OUTPUT); digitalWrite(PIN_485_CB, HIGH); 
  #endif
  
  pixels.begin(); pixels.setBrightness(20); setLed(0, 0, 255);

  preferences.begin("gateway", false);
  g_bms_count = preferences.getInt("cnt", 2); 
  g_force_cell_count = preferences.getInt("cells", 15);
  g_charge_amps = preferences.getFloat("chg", 20.0);
  g_discharge_amps = preferences.getFloat("dis", 30.0);
  g_expert_mode = preferences.getBool("exp", false);
  g_cvl_voltage = preferences.getFloat("cvl", 52.5);
  g_sd_enable = preferences.getBool("sd_en", false);
  g_mqtt_enable = preferences.getBool("mq_en", false);
  g_mqtt_server = preferences.getString("mq_ip", "");
  g_mqtt_port   = preferences.getInt("mq_pt", 1883);
  g_mqtt_user   = preferences.getString("mq_us", "");
  g_mqtt_pass   = preferences.getString("mq_pw", "");

  initSD();

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  #if ARDUINO_ESP32_MAJOR >= 2
    RS485.setRxBufferSize(1024);
  #endif

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); 
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) { twai_start(); Serial.println("CAN Init OK"); } 
  else { Serial.println("CAN Init Fail"); }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME); 
  WiFi.begin(FIXED_SSID, FIXED_PASS);
  int tryCount=0;
  while(WiFi.status()!=WL_CONNECTED && tryCount<10) { delay(500); tryCount++; Serial.print("."); }
  
  if(WiFi.status() == WL_CONNECTED) {
      setLed(0, 255, 0); 
      Serial.println("\nWiFi Connected");
      if(MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
      if(g_mqtt_enable && g_mqtt_server != "") mqtt.setServer(g_mqtt_server.c_str(), g_mqtt_port);
      configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  } else { 
      setLed(255,0,0); 
      WiFiManager wm; wm.autoConnect("Victron-Gateway-Setup"); 
  }

  server.on("/",    handleRoot);
  server.on("/data",handleData);
  server.on("/sim", handleSim);
  server.on("/save",handleSave);
  server.on("/update", HTTP_GET, handleUpdate);
  server.on("/update2", HTTP_POST, handleUpdate2, handleUpdateUpload);
  server.on("/sd/download", HTTP_GET, handleSDDownload);
  server.on("/sd/clear", HTTP_GET, handleSDClear);
  
  server.begin();
  
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    esp_task_wdt_config_t wdt_config = { .timeout_ms = WDT_TIMEOUT * 1000, .idle_core_mask = (1 << 0), .trigger_panic = true };
    esp_task_wdt_init(&wdt_config);
  #else
    esp_task_wdt_init(WDT_TIMEOUT, true);
  #endif
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset(); 
  server.handleClient();
  unsigned long now = millis();
  
  if (now - last_hist_time > HISTORY_INTERVAL) { last_hist_time = now; updateHistory(); }
  calculateEnergy();
  
  if (g_sd_enable && (now - last_sd_time > SD_LOG_INTERVAL)) { last_sd_time = now; writeLogToSD(); }
  
  if (g_mqtt_enable && WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) { if(now - last_mqtt_time > 5000) { last_mqtt_time = now; mqttReconnect(); } } 
      else { mqtt.loop(); if(now - last_mqtt_time > MQTT_INTERVAL) { last_mqtt_time = now; sendMqttData(); } }
  }

  if (!simulation_active && (now - last_poll_time > POLL_INTERVAL)) {
    last_poll_time = now;
    for (int i = 0; i < g_bms_count; i++) {
      esp_task_wdt_reset();
      
      #if BOARD_TYPE == 2
        digitalWrite(PIN_RS485_DIR, HIGH);
      #endif
      
      RS485.flush(); while(RS485.available()) RS485.read();
      RS485.print(POLL_CMDS[i]); 
      RS485.flush();
      
      #if BOARD_TYPE == 2
        digitalWrite(PIN_RS485_DIR, LOW);
      #endif

      unsigned long start = millis();
      String resp = "";
      while (millis() - start < RS485_TIMEOUT) { 
        if (RS485.available()) { char c = RS485.read(); if (c == '\r') break; if (c != '~') resp += c; } else { yield(); }
      }
      if (resp.length() > 0) parseTopband(resp, i);
      else addToLog("Timeout BMS " + String(i), true); 
      delay(BUS_GUARD_TIME);
    }
  }

  if (now - last_can_time > 500) {
    last_can_time = now;
    calculateVictronData(); 
    sendVictronCAN();       
  }
}