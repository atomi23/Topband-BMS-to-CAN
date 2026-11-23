#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

class String {
  std::string value;

 public:
  String() = default;
  String(const char *s) : value(s) {}
  String(int n) : value(std::to_string(n)) {}
  String(unsigned long n) : value(std::to_string(n)) {}
  String(float n, int digits = 2) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, n);
    value = buf;
  }
  int length() const { return value.size(); }
  void reserve(int size) { value.reserve(size); }
  char operator[](int i) const { return value[i]; }
  void operator+=(char c) { value += c; }
  void operator+=(const String &s) { value += s.value; }
  friend String operator+(String a, const String &b) { a += b; return a; }
  void replace(const std::string &from, const std::string &to) {
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
      value.replace(pos, from.size(), to);
      pos += to.size();
    }
  }
  const std::string &bytes() const { return value; }
};

struct {
  void send(int, const char *, const String &body) { std::cout << body.bytes() << '\n'; }
} server;
bool readLocalTime(struct tm *) { return false; }
bool getLocalTime(struct tm *t) { return readLocalTime(t); }
unsigned long millis() { return 0; }

struct {
  int activePacks = 1;
  float totalCurrent, totalPower, avgSOC, avgSOH, avgVoltage, remainCapacity,
      totalCapacity, avgTemp, maxChargeCurrent, maxDischargeCurrent;
} victronData;
struct {
  bool valid = true;
  unsigned long last_seen = 0;
  int soc = 80, soh = 100, temp_count = 7, cell_count = 1;
  float current, voltage, minCellV, maxCellV, avgTemp, temps[7], cells[1];
} bms[1];

const int HISTORY_LEN = 960;
const bool HAS_SD_CARD = true;
int g_bms_count = 1, g_force_cell_count, g_temp_mode, g_can_protocol, g_theme_id,
    g_timezone_offset, g_mqtt_port, g_soc_source_mode, alarm_flags, historyIdx;
unsigned long g_auto_balance_last_ts;
float g_runtime_cvl, g_cvl_voltage, g_runtime_proto_ccl_cap, g_runtime_proto_dcl_cap,
    g_maint_target_v, g_charge_amps, g_discharge_amps, g_safe_volt, g_safe_cell,
    g_tc_min, g_tc_max, g_td_min, g_td_max, g_safe_drift, daily_in[7], daily_out[7];
int16_t powerHistory[HISTORY_LEN];
bool g_victron_enable = true, g_runtime_proto_uv_hit, g_maint_charge_mode,
    g_runtime_auto_balance_due, g_expert_mode, g_serial_debug, g_sd_spy, g_sd_enable,
    g_mqtt_enable, g_mqtt_full, g_ha_enable, g_auto_balance_enable, g_easy_mode,
    g_setup_done, can_error_flag, sd_ok, sd_error_flag, simulation_active;
String g_ntp_server = "pool.ntp.org", g_mqtt_server = "broker",
    g_mqtt_user, debug_log, debug_can_status = "CAN OK";

#include "data_json_paths.h"

int main() {
  handleData();
  g_mqtt_user = "ACME\\operator";
  handleData();

  String special = "\"\\ä";
  for (int c = 0; c < 0x20; ++c) special += static_cast<char>(c);
  g_mqtt_user = g_mqtt_server = g_ntp_server = debug_can_status = special;
  debug_log = "<div class='log-ok'>" + special + "</div>";
  handleData();
}
