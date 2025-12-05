#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <driver/twai.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <PubSubClient.h> // WICHTIG: Bibliothek installieren!

// =====================================================================
// KONFIGURATION (FEST)
// =====================================================================
const char* FIXED_SSID = "SSID";     
const char* FIXED_PASS = "PSWDWLAN";
const char* HOSTNAME   = "victron-gateway"; 

// =====================================================================
// HARDWARE: LILYGO T-CAN485
// =====================================================================
#define RS485_TX_PIN   22
#define RS485_RX_PIN   21

// LilyGo Spezifisch: Auto-Flow Control
#define PIN_PWR_BOOST  16 
#define PIN_485_EN     19 
#define PIN_485_CB     17 // HIGH = Auto Direction

#define CAN_TX_PIN     27
#define CAN_RX_PIN     26
#define PIN_CAN_MODE   23 
#define LED_PIN        4
#define NUM_LEDS       1
#define MAX_BMS        16

#define RS485_BAUD     9600

// TIMING
#define POLL_INTERVAL    3000  
#define RS485_TIMEOUT    600   
#define BUS_GUARD_TIME   200   
#define MQTT_INTERVAL    5000 // Sende alle 5s MQTT

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

// SETTINGS
int   g_bms_count        = 2;
float g_charge_amps      = 20.0;
float g_discharge_amps   = 30.0;
int   g_force_cell_count = 15;
float g_cvl_voltage      = 52.5; 
bool  g_expert_mode      = false;

// MQTT SETTINGS
String g_mqtt_server     = "";
int    g_mqtt_port       = 1883;
String g_mqtt_user       = "";
String g_mqtt_pass       = "";
bool   g_mqtt_enable     = false;

bool  simulation_active = false;
unsigned long last_poll_time = 0;
unsigned long last_can_time  = 0;
unsigned long last_mqtt_time = 0;
String debug_log = "";
String debug_can_status = "Init...";
bool   can_error_flag = false;

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
  float totalCapacity;
  int activePacks;
} victronData;

// =====================================================================
// HELFER
// =====================================================================
void setLed(int r, int g, int b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b)); pixels.show();
}

void addToLog(String msg, bool error) {
    String color = error ? "#ff5555" : "#55ff55";
    String timeStr = String(millis() / 1000.0, 1);
    String entry = "<div style='color:" + color + "; border-bottom:1px solid #333; padding:2px;'>[" + timeStr + "] " + msg + "</div>";
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
// PARSER
// =====================================================================
void parseTopband(String raw, int addr) {
  if (raw.length() < 20) { addToLog("BMS " + String(addr) + ": Kurz", true); return; }

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
    float max_t = -99;
    float sum_t = 0;
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
// DATA AGGREGATION
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
      victronData.totalCapacity = 0;
  }
}

// =====================================================================
// MQTT 
// =====================================================================
void mqttReconnect() {
  if (!g_mqtt_enable || g_mqtt_server == "") return;
  
  // Non-blocking check
  if (mqtt.connect("VictronGateway", g_mqtt_user.c_str(), g_mqtt_pass.c_str())) {
      addToLog("MQTT Verbunden", false);
      mqtt.publish("victron/gateway/status", "online");
  }
}

void sendMqttData() {
    if(!g_mqtt_enable || !mqtt.connected()) return;

    // System Data
    String json = "{";
    json += "\"soc\":" + String(victronData.avgSOC, 1) + ",";
    json += "\"voltage\":" + String(victronData.avgVoltage, 2) + ",";
    json += "\"current\":" + String(victronData.totalCurrent, 1) + ",";
    json += "\"power\":" + String(victronData.avgVoltage * victronData.totalCurrent, 0) + ",";
    json += "\"active_packs\":" + String(victronData.activePacks);
    json += "}";
    mqtt.publish("victron/system", json.c_str());
    
    // Einzelne Packs (nur gültige)
    for(int i=0; i<g_bms_count; i++) {
        if(bms[i].valid) {
            String b = "{";
            b += "\"voltage\":" + String(bms[i].voltage, 2) + ",";
            b += "\"soc\":" + String(bms[i].soc) + ",";
            b += "\"current\":" + String(bms[i].current, 1) + ",";
            b += "\"temp\":" + String(bms[i].avgTemp, 1) + ",";
            b += "\"cell_min\":" + String(bms[i].minCellV, 3) + ",";
            b += "\"cell_max\":" + String(bms[i].maxCellV, 3);
            b += "}";
            String topic = "victron/bms/" + String(i);
            mqtt.publish(topic.c_str(), b.c_str());
        }
    }
}

void sendCanFrame(uint32_t id, uint8_t* data) {
    twai_message_t msg;
    msg.identifier = id; msg.extd=0; msg.data_length_code=8;
    memcpy(msg.data, data, 8);
    esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(10));
    
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
    
    debug_can_status = "Sending OK (" + String(victronData.activePacks) + " Packs, CVL " + String(cvl_val,1) + "V)";
}

// =====================================================================
// WEB UI
// =====================================================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Topband-Victron Gateway</title>
  <style>
    body { font-family: 'Segoe UI', sans-serif; background-color: #121212; color: #e0e0e0; margin: 0; padding: 10px; padding-bottom: 60px; }
    h1 { text-align: center; color: #fff; margin: 10px 0; font-size:1.4rem; }
    
    .top-controls { display:flex; justify-content:center; margin-bottom:15px; }
    .toggle-btn { background: #444; color: #fff; border: 1px solid #666; padding: 6px 16px; border-radius: 20px; cursor: pointer; text-transform: uppercase; font-size: 0.8rem; letter-spacing: 1px; transition: all 0.2s;}
    .toggle-btn:hover { background: #666; border-color:#888; }

    .dashboard { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 15px; }
    .bms-card { background-color: #1e1e1e; border-radius: 12px; padding: 15px; border: 1px solid #333; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    .bms-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #444; padding-bottom: 8px; margin-bottom: 8px; }
    .bms-title { font-weight: bold; color: #fff; }
    .status-badge { padding: 3px 6px; border-radius: 4px; font-size: 0.75rem; }
    .online { background-color: #2e7d32; color: white; }
    .offline { background-color: #c62828; color: white; }
    
    .c-blue { color: #2979ff; }
    .c-cyan { color: #4dabf7; }
    .c-red { color: #ff6b6b; }
    .c-orange { color: #ffab40; }
    
    .can-status-box { text-align:center; padding:10px; border-radius:8px; margin-bottom:20px; font-weight:bold; }
    .can-ok { background: #1b5e20; color: #fff; border: 1px solid #2e7d32; }
    .can-err { background: #b71c1c; color: #fff; border: 1px solid #d32f2f; animation: blink 2s infinite; }
    @keyframes blink { 0% {opacity:1} 50% {opacity:0.7} 100% {opacity:1} }

    .pack-voltage-container { text-align: center; margin: 10px 0; }
    .pack-voltage-value { font-size: 2rem; font-weight: bold; color: #2979ff; text-shadow: 0 0 10px rgba(41, 121, 255, 0.4); }
    .main-stats { display: flex; justify-content: space-around; background: #252525; padding: 8px; border-radius: 8px; margin-bottom: 10px; }
    .stat-box { text-align: center; }
    .stat-value { font-weight: bold; display: block; }
    .stat-label { font-size: 0.7rem; color: #888; }
    
    .cell-stats-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4px; background: #2a2a2a; padding: 8px; border-radius: 8px; text-align: center; margin-bottom: 10px; }
    .cs-val { font-weight: bold; }
    .cs-lbl { font-size: 0.7rem; color: #aaa; }
    
    .cells-wrapper { margin-top: 15px; border-top: 1px solid #444; padding-top: 10px; }
    .cells-title { font-size: 0.9em; color: #aaa; margin-bottom: 8px; }
    .cell-grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 4px; }
    .cell-box { background: #252525; padding: 4px; border-radius: 3px; text-align: center; font-size: 0.75em; border: 1px solid #333; }
    .cell-num { font-size: 0.7em; color: #888; display:block; }
    .cell-val { font-weight: bold; }
    .cell-min { border: 1px solid #4dabf7; background: rgba(77, 171, 247, 0.1); }
    .cell-min .cell-val { color: #4dabf7; }
    .cell-max { border: 1px solid #ff6b6b; background: rgba(255, 107, 107, 0.1); }
    .cell-max .cell-val { color: #ff6b6b; }
    
    .temp-grid { display: flex; flex-wrap: wrap; gap: 5px; font-size: 0.8rem; color: #ccc; margin-top:10px; }
    .temp-item { background: #252525; padding: 2px 6px; border-radius: 4px; }
    
    details.bms-list-item { background: #1e1e1e; border: 1px solid #333; border-radius: 8px; margin-bottom: 8px; padding: 0; overflow: hidden; transition: all 0.2s; }
    details.bms-list-item summary { 
        padding: 10px 15px; cursor: pointer; font-weight: bold; outline: none; list-style: none; display: flex; align-items: center; background: #222;
        font-family: 'Segoe UI', monospace; font-size: 0.95em;
    }
    details.bms-list-item summary::-webkit-details-marker { display: none; }
    details.bms-list-item[open] summary { background: #2a2a2a; border-bottom: 1px solid #333; }
    
    .list-row { display: grid; grid-template-columns: 80px 1fr 1fr 1fr 1fr 20px; width: 100%; align-items: center; gap:5px; }
    .list-id { color: #fff; font-weight:bold; }
    .list-val { text-align: right; white-space:nowrap; }
    .list-arrow { text-align: right; color: #666; transition: transform 0.2s; }
    details.bms-list-item[open] .list-arrow { transform: rotate(180deg); }
    .bms-content-inner { padding: 15px; }

    .card-header-stats { display: flex; justify-content: space-between; border-bottom: 1px solid #444; padding-bottom: 10px; margin-bottom: 10px; font-size:0.9em;}
    .chs-item { text-align:center; }
    .chs-lbl { font-size:0.7em; color:#888; display:block; }

    .victron-card { background: #1a237e; border-color: #3949ab; }
    .logbox { background: #000; color: #0f0; font-family: monospace; font-size: 0.7rem; height: 100px; overflow-y: scroll; padding: 5px; border: 1px solid #444; border-radius: 5px; margin-bottom: 10px; }
    
    details.settings-box { background: #222; padding: 10px; border-radius: 8px; margin-top: 30px; border: 1px solid #444; }
    .settings-form { display: grid; gap: 10px; margin-top: 10px; max-width: 400px; }
    .input-group { display: flex; justify-content: space-between; align-items: center; }
    input[type=number], input[type=checkbox], input[type=text] { padding: 5px; background: #333; color: white; border: 1px solid #555; border-radius: 4px; }
    button.save-btn { background: #2e7d32; color: white; border: none; padding: 8px; border-radius: 4px; cursor: pointer; width: 100%; font-weight: bold; margin-top:10px;}
    
    .legend { font-size: 0.8em; margin-top: 20px; color: #888; border-top: 1px solid #333; padding-top: 10px; display:flex; gap:15px; flex-wrap:wrap;}
    .dot { display:inline-block; width:10px; height:10px; border-radius:50%; margin-right:5px; }
    .footer { text-align: center; font-size: 0.7em; color: #555; margin-top: 30px; padding-bottom: 20px;}
    .expert-warning { color: #f44; font-size: 0.8em; margin-top:5px; display:none; }
  </style>
</head>
<body>
  <h1>Topband-Victron Gateway</h1>
  
  <div class="top-controls">
    <button class="toggle-btn" onclick="toggleView()">Ansicht wechseln</button>
  </div>

  <div id="statusBox" class="can-status-box can-ok">Initialisiere...</div>

  <div class="dashboard" style="margin-bottom: 20px;">
     <div class="bms-card victron-card">
        <div class="bms-header"><span class="bms-title">Gesamtsystem (An Victron)</span></div>
        <div class="main-stats" style="background: rgba(0,0,0,0.2);">
           <div class="stat-box"><span class="stat-value" id="total-curr">-- A</span><span class="stat-label">Summe Strom</span></div>
           <div class="stat-box"><span class="stat-value" id="avg-soc">-- %</span><span class="stat-label">Ø SOC</span></div>
           <div class="stat-box"><span class="stat-value" id="total-cap">-- Ah</span><span class="stat-label">Summe Kapazität</span></div>
        </div>
     </div>
  </div>

  <div id="container" class="dashboard"></div>

  <div class="bms-card" style="margin-top:20px;">
    <div class="bms-header"><span class="bms-title">🔍 Diagnose / Live Log</span></div>
    <div id="logbox" class="logbox">Warte auf Daten...</div>
  </div>

  <div class="legend">
    <div style="width:100%; margin-bottom:5px;"><strong>Legende:</strong></div>
    <span><span class="dot" style="background:#4dabf7"></span>Min. Zelle</span>
    <span><span class="dot" style="background:#ff6b6b"></span>Max. Zelle/Drift</span>
    <span><span class="dot" style="background:#ffab40"></span>Temp Ø</span>
    <span><span class="dot" style="background:#2979ff"></span>Spannung</span>
  </div>

  <details class="settings-box">
    <summary>⚙ Einstellungen & Hilfe</summary>
    <div style="margin-bottom:15px; font-size:0.9em; color:#aaa; padding:10px; background:#333; border-radius:4px;">
        <strong>Hilfe:</strong><br>
        - Grüne LED blinkt: CAN OK (Senden erfolgreich)<br>
        - Rote LED an: CAN Fehler (Kabel prüfen, 120Ω Widerstand?)<br>
        - Drift: Differenz zwischen höchster und niedrigster Zelle.<br>
    </div>
    
    <form class="settings-form" action="/save" method="POST">
      <div class="input-group"><label>Anzahl BMS:</label><input type="number" name="count" value=")rawliteral" + String(g_bms_count) + R"rawliteral(" min="1" max="16"></div>
      <div class="input-group"><label>Zellen (15/16):</label><input type="number" name="cells" value=")rawliteral" + String(g_force_cell_count) + R"rawliteral(" min="10" max="16"></div>
      <div class="input-group"><label>Ladestrom (A/Pack):</label><input type="number" name="charge" value=")rawliteral" + String(g_charge_amps, 1) + R"rawliteral(" step="0.1"></div>
      <div class="input-group"><label>Entladen (A/Pack):</label><input type="number" name="discharge" value=")rawliteral" + String(g_discharge_amps, 1) + R"rawliteral(" step="0.1"></div>
      
      <div class="input-group" style="border-top:1px solid #444; padding-top:10px; margin-top:5px;">
        <label style="color:#f88">⚠️ Experten-Modus:</label>
        <input type="checkbox" name="expert" id="expertCheck" onclick="toggleExpert()" )rawliteral" + (g_expert_mode ? "checked" : "") + R"rawliteral(>
      </div>
      
      <div class="input-group" id="cvlGroup" style="opacity:0.5; pointer-events:none;">
        <label>Ladespannung (CVL):</label>
        <input type="number" name="cvl" id="cvlInput" value=")rawliteral" + String(g_cvl_voltage, 1) + R"rawliteral(" step="0.1" min="50" max="58">
      </div>
      <div id="expertWarn" class="expert-warning">ACHTUNG: Falsche Spannung kann den Akku zerstören! Standard (15s) ist 52.5V.</div>

      <button type="submit" class="save-btn">Speichern & Neustart</button>
    </form>
  </details>
  
  <details class="settings-box">
    <summary>📡 MQTT Einstellungen</summary>
    <form class="settings-form" action="/save" method="POST">
      <div class="input-group"><label>Aktivieren:</label><input type="checkbox" name="mqtt_en" )rawliteral" + (g_mqtt_enable ? "checked" : "") + R"rawliteral(></div>
      <div class="input-group"><label>Broker IP:</label><input type="text" name="mqtt_ip" value=")rawliteral" + g_mqtt_server + R"rawliteral("></div>
      <div class="input-group"><label>Port:</label><input type="number" name="mqtt_port" value=")rawliteral" + String(g_mqtt_port) + R"rawliteral("></div>
      <div class="input-group"><label>User:</label><input type="text" name="mqtt_user" value=")rawliteral" + g_mqtt_user + R"rawliteral("></div>
      <div class="input-group"><label>Pass:</label><input type="text" name="mqtt_pass" value=")rawliteral" + g_mqtt_pass + R"rawliteral("></div>
      <button type="submit" class="save-btn">Speichern</button>
    </form>
  </details>

  <div class="footer">
    <p><strong>Disclaimer:</strong> Dies ist ein DIY-Projekt. Nutzung auf eigene Gefahr.<br>Nur für private, nicht kommerzielle Nutzung.<br>Keine Gewährleistung für Schäden an Batterien oder Invertern.</p>
    <p>Projekt auf GitHub: <a href="https://github.com/atomi23/Topband-BMS-to-CAN" target="_blank" style="color:#888">atomi23/Topband-BMS-to-CAN</a></p>
  </div>

  <script>
    let viewMode = localStorage.getItem('viewMode') || 'list';

    function toggleExpert() {
        const chk = document.getElementById('expertCheck');
        const grp = document.getElementById('cvlGroup');
        const warn = document.getElementById('expertWarn');
        if(chk.checked) {
            grp.style.opacity = '1'; grp.style.pointerEvents = 'auto'; warn.style.display = 'block';
        } else {
            grp.style.opacity = '0.5'; grp.style.pointerEvents = 'none'; warn.style.display = 'none';
        }
    }
    toggleExpert();

    function toggleView() {
        viewMode = (viewMode === 'cards') ? 'list' : 'cards';
        localStorage.setItem('viewMode', viewMode);
        updateDashboard();
    }

    function getTempLabel(idx, count) {
       if (count >= 6) {
           if (idx < 4) return "Zelle " + (idx+1);
           if (idx === 4) return "Bal";
           if (idx === 5) return "MOS";
           if (idx === 6) return "Env";
       }
       return "T" + (idx+1);
    }

    function getSocColor(soc) {
        if(soc >= 50) return '#4f4';
        if(soc >= 20) return '#fb0';
        return '#f44';
    }

    // SHARED CONTENT GENERATOR
    function generateBMSContent(bms) {
        let tempHtml = '';
        if(bms.temps) {
            bms.temps.forEach((t, i) => {
                tempHtml += `<div class="temp-item">${getTempLabel(i, bms.temps.length)}: ${t.toFixed(1)}°C</div>`;
            });
        }

        let cellGridHtml = '';
        if(bms.cells) {
           bms.cells.forEach((v, i) => {
               let cssClass = 'cell-box';
               if(i+1 === bms.min_idx) cssClass += ' cell-min';
               if(i+1 === bms.max_idx) cssClass += ' cell-max';
               cellGridHtml += `<div class="${cssClass}"><span class="cell-num">${i+1}</span><span class="cell-val">${v.toFixed(3)}</span></div>`;
           });
        }

        return `
            <div class="pack-voltage-container">
              <div class="pack-voltage-value">${bms.pack_v.toFixed(2)} V</div>
              <div style="font-size:0.8em; color:#888;">${bms.rem_ah.toFixed(1)} / ${bms.full_ah.toFixed(1)} Ah</div>
            </div>
            <div class="main-stats">
              <div class="stat-box"><span class="stat-value">${bms.current.toFixed(1)} A</span><span class="stat-label">Strom</span></div>
              <div class="stat-box"><span class="stat-value" style="color:${getSocColor(bms.soc)}">${bms.soc.toFixed(0)} %</span><span class="stat-label">SOC</span></div>
              <div class="stat-box"><span class="stat-value">${bms.soh.toFixed(0)} %</span><span class="stat-label">SOH</span></div>
            </div>
            <div class="cell-stats-grid">
                <div><div class="cs-val c-cyan">${bms.min_cell.toFixed(3)} V</div><div class="cs-lbl">Min</div></div>
                <div><div class="cs-val">${bms.avg_cell.toFixed(3)} V</div><div class="cs-lbl">Ø</div></div>
                <div><div class="cs-val c-red">${bms.max_cell.toFixed(3)} V</div><div class="cs-lbl">Max</div></div>
            </div>
            <div class="cells-wrapper">
                <div class="cells-title">Zellen</div>
                <div class="cell-grid">${cellGridHtml}</div>
            </div>
            <div class="temp-grid">${tempHtml}</div>
        `;
    }

    function updateDashboard() {
      const container = document.getElementById('container');
      const statusBox = document.getElementById('statusBox');
      
      if(viewMode === 'list') container.style.display = 'block'; 
      else container.style.display = 'grid';

      // SAVE STATE
      const openDetails = {};
      document.querySelectorAll('details.bms-list-item').forEach(el => {
          const id = el.getAttribute('id');
          if(el.hasAttribute('open')) openDetails[id] = true;
      });

      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('total-curr').innerText = data.victron.total_amps.toFixed(1) + " A";
          document.getElementById('avg-soc').innerText = data.victron.avg_soc.toFixed(1) + " %";
          document.getElementById('total-cap').innerText = data.victron.total_cap.toFixed(1) + " Ah";
          document.getElementById('logbox').innerHTML = data.log;
          
          if(data.can_error) {
              statusBox.className = "can-status-box can-err";
              statusBox.innerText = "CAN FEHLER (Keine Antwort / Kein Widerstand?)";
          } else {
              statusBox.className = "can-status-box can-ok";
              statusBox.innerText = "CAN OK (" + data.can_status + ")";
          }

          let htmlContent = '';

          data.bms.forEach((bms, index) => {
            const detailId = `bms-detail-${index}`;
            const innerContent = bms.online ? generateBMSContent(bms) : '<div style="text-align:center;padding:20px;">Keine Verbindung</div>';
            
            // PREP HEADER VARS
            let socColor = getSocColor(bms.soc);
            let delta = (bms.max_cell - bms.min_cell).toFixed(3);
            let avgTemp = bms.avg_temp ? bms.avg_temp.toFixed(1) : '-';

            if(viewMode === 'list') {
                if(!bms.online) {
                    htmlContent += `<details id="${detailId}" class="bms-list-item" style="opacity:0.5"><summary><div class="list-row"><span class="list-id">BMS ${index+1}</span><span style="color:#f44">OFFLINE</span><span>-</span><span>-</span><span>-</span></div></summary></details>`;
                } else {
                    htmlContent += `
                    <details id="${detailId}" class="bms-list-item">
                        <summary>
                            <div class="list-row">
                                <span class="list-id">BMS ${index+1}</span>
                                <span class="list-val" style="color:${socColor}">${bms.soc}%</span>
                                <span class="list-val c-blue">${bms.pack_v.toFixed(2)}V</span>
                                <span class="list-val c-red" title="Drift / Delta">∆${delta}V</span>
                                <span class="list-val c-orange" title="Ø Temp">${avgTemp}°C</span>
                                <span class="list-arrow">▼</span>
                            </div>
                        </summary>
                        <div class="bms-content-inner">
                            ${innerContent}
                        </div>
                    </details>`;
                }
            } else {
                // CARD VIEW (Header erweitert!)
                if(!bms.online) {
                    htmlContent += `<div class="bms-card" style="opacity:0.6"><div class="bms-header"><span class="bms-title">BMS #${index+1}</span><span class="status-badge offline">OFFLINE</span></div>${innerContent}</div>`;
                } else {
                    htmlContent += `
                      <div class="bms-card">
                        <div class="bms-header">
                          <span class="bms-title">BMS #${index + 1}</span>
                          <span class="status-badge online">ONLINE</span>
                        </div>
                        <!-- Extended Header Stats -->
                        <div class="card-header-stats">
                            <div class="chs-item"><span class="chs-lbl">Drift</span><span class="c-red">∆${delta}V</span></div>
                            <div class="chs-item"><span class="chs-lbl">Ø Temp</span><span class="c-orange">${avgTemp}°C</span></div>
                        </div>
                        ${innerContent}
                      </div>`;
                }
            }
          });
          
          container.innerHTML = htmlContent;

          // RESTORE STATE
          Object.keys(openDetails).forEach(id => {
              const el = document.getElementById(id);
              if(el) el.setAttribute('open', '');
          });
        });
    }
    
    setInterval(updateDashboard, 2000);
    updateDashboard();
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
  json += "\"avg_soc\": " + String(victronData.avgSOC, 1) + ",";
  json += "\"total_cap\": " + String(victronData.totalCapacity, 1) + ",";
  json += "\"ccl\": " + String(victronData.maxChargeCurrent, 1);
  json += "}, ";
  json += "\"log\": \"" + debug_log + "\", ";
  json += "\"can_status\": \"" + debug_can_status + "\", ";
  json += "\"can_error\": " + String(can_error_flag ? "true" : "false") + ", ";

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
  
  // MQTT Settings
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
  
  preferences.putBool("mq_en", g_mqtt_enable);
  preferences.putString("mq_ip", g_mqtt_server);
  preferences.putInt("mq_pt", g_mqtt_port);
  preferences.putString("mq_us", g_mqtt_user);
  preferences.putString("mq_pw", g_mqtt_pass);

  server.send(200, "text/html", "<h1>Gespeichert! Starte neu...</h1><script>setTimeout(function(){window.location.href='/';}, 3000);</script>");
  delay(1000); ESP.restart();
}

// =====================================================================
// SETUP & LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Topband Victron Gateway V64 MQTT Final ===");
  
  // LilyGo Pins
  pinMode(PIN_PWR_BOOST, OUTPUT); digitalWrite(PIN_PWR_BOOST, HIGH);
  pinMode(PIN_485_EN, OUTPUT); digitalWrite(PIN_485_EN, HIGH);
  pinMode(PIN_485_CB, OUTPUT); digitalWrite(PIN_485_CB, HIGH); 
  
  pixels.begin(); pixels.setBrightness(20); setLed(0, 0, 255);

  preferences.begin("gateway", false);
  g_bms_count = preferences.getInt("cnt", 2); 
  g_force_cell_count = preferences.getInt("cells", 15);
  g_charge_amps = preferences.getFloat("chg", 20.0);
  g_discharge_amps = preferences.getFloat("dis", 30.0);
  g_expert_mode = preferences.getBool("exp", false);
  g_cvl_voltage = preferences.getFloat("cvl", 52.5);
  
  // Load MQTT
  g_mqtt_enable = preferences.getBool("mq_en", false);
  g_mqtt_server = preferences.getString("mq_ip", "");
  g_mqtt_port   = preferences.getInt("mq_pt", 1883);
  g_mqtt_user   = preferences.getString("mq_us", "");
  g_mqtt_pass   = preferences.getString("mq_pw", "");

  // RS485 INIT
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  #if ARDUINO_ESP32_MAJOR >= 2
    RS485.setRxBufferSize(1024);
  #endif

  // CAN INIT
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); 
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
      twai_start();
      Serial.println("CAN Init OK");
  } else {
      Serial.println("CAN Init Fail");
  }

  // WIFI INIT & mDNS
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME); 
  WiFi.begin(FIXED_SSID, FIXED_PASS);
  int tryCount=0;
  Serial.print("Connecting to WiFi");
  while(WiFi.status()!=WL_CONNECTED && tryCount<10) { delay(500); tryCount++; Serial.print("."); }
  
  if(WiFi.status() == WL_CONNECTED) {
      setLed(0, 255, 0); 
      Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
      if(MDNS.begin(HOSTNAME)) {
          Serial.println("mDNS responder started: http://" + String(HOSTNAME) + ".local");
          MDNS.addService("http", "tcp", 80);
      }
      
      // MQTT Init
      if(g_mqtt_enable && g_mqtt_server != "") {
          mqtt.setServer(g_mqtt_server.c_str(), g_mqtt_port);
      }
  } else { 
      setLed(255,0,0); 
      Serial.println("\nWiFi Failed -> AP Mode");
      WiFiManager wm; wm.autoConnect("Victron-Gateway-Setup"); 
  }

  server.on("/",    handleRoot);
  server.on("/data",handleData);
  server.on("/sim", handleSim);
  server.on("/save",handleSave);
  server.begin();
  Serial.println("HTTP Server started");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  
  // MQTT Handler
  if (g_mqtt_enable && WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) {
          if(now - last_mqtt_time > 5000) {
              last_mqtt_time = now;
              mqttReconnect();
          }
      } else {
          mqtt.loop();
          if(now - last_mqtt_time > MQTT_INTERVAL) {
              last_mqtt_time = now;
              sendMqttData();
          }
      }
  }

  // RS485 Polling
  if (!simulation_active && (now - last_poll_time > POLL_INTERVAL)) {
    last_poll_time = now;
    
    for (int i = 0; i < g_bms_count; i++) {
      String resp = "";
      
      RS485.flush();
      while(RS485.available()) RS485.read(); // Clean buffer
      
      RS485.print(POLL_CMDS[i]); 

      unsigned long start = millis();
      while (millis() - start < RS485_TIMEOUT) { 
        if (RS485.available()) {
          char c = RS485.read();
          if (c == '\r') break;
          if (c != '~') resp += c;
        } else { 
            yield(); 
        }
      }
      
      if (resp.length() > 0) parseTopband(resp, i);
      else addToLog("Timeout BMS " + String(i), true); 
      
      delay(BUS_GUARD_TIME);
    }
  }

  // Victron CAN Senden (Alle 500ms)
  if (now - last_can_time > 500) {
    last_can_time = now;
    calculateVictronData(); 
    sendVictronCAN();       
  }
}