#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

namespace sketch {

struct String {
  std::string value;
  String(const char *text = "") : value(text) {}
  String(const std::string &text) : value(text) {}
  template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
  String(T number) : value(std::to_string(number)) {}
  const char *c_str() const { return value.c_str(); }
  bool operator==(const char *other) const { return value == other; }
  bool operator!=(const char *other) const { return value != other; }
  String operator+(const String &other) const { return value + other.value; }
};

uint32_t fake_millis = 0;
time_t fake_epoch = 0;
uint32_t millis() { return fake_millis; }
void delay(uint32_t ms) { fake_millis += ms; }
time_t time(time_t *out) {
  if (out) *out = fake_epoch;
  return fake_epoch;
}

// Copy of getLocalTime() from arduino-esp32 3.3.12 cores/esp32/esp32-hal-time.c
bool getLocalTime(struct tm *info, uint32_t ms = 5000) {
  uint32_t start = millis();
  time_t now;
  while ((millis() - start) <= ms) {
    time(&now);
    localtime_r(&now, info);
    if (info->tm_year > (2016 - 1900)) {
      return true;
    }
    delay(10);
  }
  return false;
}

long configured_offset = 0;
std::string configured_server;
int configure_calls = 0;
void configTime(long gmt_offset, int daylight_offset, const char *server) {
  configured_offset = gmt_offset;
  configured_server = server;
  ++configure_calls;
}

enum { WL_CONNECTED = 3, WIFI_STA = 1 };
struct WiFiManager {
  void setAPCallback(void (*)(WiFiManager *)) {}
  void setConnectTimeout(int) {}
  void setConfigPortalTimeout(int) {}
  bool autoConnect(const char *) { return true; }
};
struct {
  void setHostname(const char *) {}
  void mode(int) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void disconnect(bool, bool) {}
  void begin(const char *, const char *) {}
  int status() { return WL_CONNECTED; }
} WiFi;
struct {
  bool begin(const char *) { return true; }
  void addService(const char *, const char *, int) {}
} MDNS;
struct {
  void println(const char *) {}
} Serial;
struct {
  std::map<std::string, String> strings{{"ntp", "de.pool.ntp.org"}};
  std::map<std::string, int> ints{{"tz", 2}};
  void begin(const char *, bool) {}
  void end() {}
  int getInt(const char *key, int fallback) { return ints.count(key) ? ints[key] : fallback; }
  float getFloat(const char *, float fallback) { return fallback; }
  bool getBool(const char *, bool fallback) { return fallback; }
  uint32_t getULong(const char *, uint32_t fallback) { return fallback; }
  String getString(const char *key, const char *fallback) { return strings.count(key) ? strings[key] : String(fallback); }
} preferences;

const char *FIXED_SSID = "";
const char *FIXED_PASS = "";
const char *HOSTNAME = "Topband-Gateway";
void configModeCallback(WiFiManager *) {}
void setLed(int, int, int) {}

struct { double totalPower = 0; } victronData;
float daily_in[7];
float daily_out[7];
unsigned long last_energy_calc = 0;
unsigned long last_flash_save = 0;
int current_day = -1;
int shifted_days = 0;
void saveHistory(bool) {}
void shiftHistory() { ++shifted_days; }

#include "ntp_time_paths.h"

}  // namespace sketch

using namespace sketch;

int failures = 0;

void expect(bool ok, const std::string &what) {
  std::cout << (ok ? "ok   " : "FAIL ") << what << '\n';
  if (!ok) ++failures;
}

template <class F>
uint32_t elapsed(F run) {
  uint32_t start = fake_millis;
  run();
  return fake_millis - start;
}

int main() {
  setenv("TZ", "UTC", 1);
  tzset();

  bootNetworkAndSettings();
  expect(configure_calls == 1 && configured_server == "de.pool.ntp.org" && configured_offset == 2 * 3600,
         "boot: configTime uses saved NTP settings (got " + configured_server + ", " +
             std::to_string(configured_offset) + " s, " + std::to_string(configure_calls) + " call)");

  fake_millis = 1000;
  calculateEnergy();
  fake_epoch = 0;
  std::string unsynced;
  unsigned long unsynced_ts = 1;
  uint32_t str_ms = elapsed([&] { unsynced = getTimeStr().value; });
  uint32_t data_ms = elapsed([&] { unsynced_ts = dataTimestamp(); });
  uint32_t energy_ms = elapsed([] { calculateEnergy(); });
  expect(str_ms == 0 && unsynced == "1s", "unsynced getTimeStr: " + std::to_string(str_ms) + " ms, \"" + unsynced + "\"");
  expect(data_ms == 0 && unsynced_ts == 0, "unsynced /data timestamp: " + std::to_string(data_ms) + " ms, ts=" + std::to_string(unsynced_ts));
  expect(energy_ms == 0 && current_day == -1, "unsynced calculateEnergy: " + std::to_string(energy_ms) + " ms");

  fake_epoch = 1795422896;  // 2026-11-23 08:34:56 UTC
  std::string synced;
  unsigned long synced_ts = 0;
  str_ms = elapsed([&] { synced = getTimeStr().value; });
  data_ms = elapsed([&] { synced_ts = dataTimestamp(); });
  energy_ms = elapsed([] { calculateEnergy(); });
  expect(str_ms == 0 && synced == "08:34:56", "synced getTimeStr: " + std::to_string(str_ms) + " ms, \"" + synced + "\"");
  expect(data_ms == 0 && synced_ts == 1795422896UL, "synced /data timestamp: " + std::to_string(data_ms) + " ms, ts=" + std::to_string(synced_ts));
  expect(energy_ms == 0 && current_day == 23 && shifted_days == 0, "synced calculateEnergy: day " + std::to_string(current_day));
  fake_epoch += 24 * 3600;
  calculateEnergy();
  expect(current_day == 24 && shifted_days == 1, "day change shifts energy history once");

  if (failures) {
    std::cout << "FAIL: " << failures << " NTP/time cases\n";
    return 1;
  }
  std::cout << "PASS: saved NTP settings applied at boot; time reads never wait\n";
}
