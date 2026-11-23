#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sketch_config.h"

using std::min;

class String : public std::string {
 public:
  using std::string::string;
  String(int value) : std::string(std::to_string(value)) {}
  int indexOf(char ch) const { return find(ch) == npos ? -1 : static_cast<int>(find(ch)); }
  String substring(int begin, int end = -1) const {
    return String(substr(begin, end == -1 ? npos : end - begin).c_str());
  }
};

static unsigned long clock_us;
unsigned long millis() { return clock_us / 1000; }
void delay(unsigned long ms) { clock_us += ms * 1000; }
void delayMicroseconds(unsigned long us) { clock_us += us; }
void yield() {}
static unsigned long last_watchdog_reset_ms, max_watchdog_gap_ms;
void esp_task_wdt_reset() {
  max_watchdog_gap_ms = std::max(max_watchdog_gap_ms, millis() - last_watchdog_reset_ms);
  last_watchdog_reset_ms = millis();
}
enum { PIN_RS485_DIR, HIGH, LOW };
void digitalWrite(int, int) {}

struct Poll { std::string command; unsigned long start_us; unsigned long done_us; unsigned long end_us; };
static bool isAuxiliary(const std::string &command) {
  return command == "alarm" || command == "system";
}

struct SerialStub {
  std::string pending;
  std::vector<Poll> polls;
  bool reply = false;
  unsigned long reply_ms = 0, auxiliary_ms = 0, ready_us = 0;
  void print(const String &command) {
    polls.push_back({command, clock_us, 0, 0});
  }
  void flush() {
    const bool auxiliary = isAuxiliary(polls.back().command);
    if (!auxiliary) clock_us += polls.back().command.size() * 10 * 1000000 / RS485_BAUD;
    polls.back().done_us = clock_us;
    ready_us = clock_us + (auxiliary ? auxiliary_ms : reply_ms) * 1000;
    if (reply) pending = "~1234\r";
  }
  bool available() const { return !pending.empty() && clock_us >= ready_us; }
  char read() {
    const char byte = pending.front();
    pending.erase(0, 1);
    if (pending.empty()) polls.back().end_us = clock_us;
    return byte;
  }
} RS485;

constexpr int g_bms_count = 12;
unsigned long last_poll_time, last_can_time, last_hist_time, last_sd_time, last_mqtt_time;
unsigned long last_alarm_poll_time, last_sysparam_poll_time;
int next_alarm_poll_bms, next_sysparam_poll_bms;
bool g_rs485_service_busy, simulation_active, g_sd_enable, g_mqtt_enable;
bool bms_sweep_in_progress;
bool g_victron_enable = true;
int g_can_protocol;
float g_cvl_voltage = 52.5f, g_runtime_cvl;
uint8_t alarm_flags;
struct {
  float maxChargeCurrent, maxDischargeCurrent, avgSOC, avgSOH, totalCapacity;
  float avgVoltage, totalCurrent, avgTemp;
} victronData;

void handleLedStatus() {}
void updateHistory() {}
void calculateEnergy() {}
void writeLogToSD() {}
void mqttReconnect() {}
void sendMqttData() {}
bool rs485ExchangeFrame(const String &cmd, String &resp, uint32_t timeout_ms);
static bool simulate_auxiliary;
static std::vector<unsigned long> alarm_refreshes[g_bms_count], sysparam_refreshes[g_bms_count];
void pollBmsAlarmStatus(int pack) {
  String response;
  if (simulate_auxiliary && rs485ExchangeFrame("alarm", response, RS485_TIMEOUT + 200)) {
    alarm_refreshes[pack].push_back(millis());
  }
}
void pollBmsSystemParameter(int pack) {
  String response;
  if (simulate_auxiliary && rs485ExchangeFrame("system", response, RS485_TIMEOUT + 200)) {
    sysparam_refreshes[pack].push_back(millis());
  }
}
void addToLog(const std::string &, bool) {}
struct { void handleClient() {} } server;
enum { WL_CONNECTED = 1 };
struct { int status() const { return 0; } } WiFi;
struct { bool connected() const { return false; } void loop() {} } mqtt;

std::vector<int> parsed;
bool payload_ok = true;
void parseTopband(const String &payload, int index) {
  payload_ok &= payload == "1234";
  parsed.push_back(index);
}

struct Frame { uint32_t id; unsigned long ms; unsigned charging; unsigned discharging; size_t packs; };
std::vector<Frame> frames;
void sendCanFrame(uint32_t id, uint8_t *data) {
  frames.push_back({id, millis(), data[2] + 256u * data[3], data[4] + 256u * data[5], parsed.size()});
}
bool last_pack_alarm;
void calculateVictronData(bool = true) {
  const auto limit = last_pack_alarm && parsed.size() == g_bms_count ? 0.0f : 10.0f * parsed.size();
  victronData.maxChargeCurrent = victronData.maxDischargeCurrent = limit;
}

#include "sketch_paths.h"

void resetTest(bool reply) {
  // Start the first sweep when the original loop also has a CAN cycle due.
  last_poll_time = 5;
  clock_us = last_poll_time * 1000;
  last_watchdog_reset_ms = millis();
  max_watchdog_gap_ms = 0;
  last_can_time = 0;
  last_alarm_poll_time = last_sysparam_poll_time = 0;
  RS485 = SerialStub{};
  RS485.reply = reply;
  parsed.clear();
  frames.clear();
  payload_ok = true;
  last_pack_alarm = false;
  victronData = {};
}

void runSweep() {
  while (RS485.polls.size() < g_bms_count && millis() < 30000) {
    loop();
    delay(1);
  }
}

bool testSweep(bool reply) {
  constexpr unsigned long period_ms = 500;
  constexpr uint32_t ids[] = {0x351, 0x355, 0x356, 0x35A, 0x35E};
  resetTest(reply);
  runSweep();
  bool polls_ok = RS485.polls.size() == g_bms_count;
  bool parses_ok = payload_ok && parsed.size() == (reply ? g_bms_count : 0);
  for (size_t i = 0; i < RS485.polls.size() && i < g_bms_count; ++i) {
    polls_ok &= RS485.polls[i].command == POLL_CMDS[i];
    if (i > 0) {
      const auto receive_ms = reply ? 0 : RS485_TIMEOUT;
      polls_ok &= RS485.polls[i].start_us >= RS485.polls[i - 1].done_us
                                          + (receive_ms + BUS_GUARD_TIME) * 1000;
    }
    if (reply && i < parsed.size()) parses_ok &= parsed[i] == static_cast<int>(i);
  }
  bool frames_ok = !frames.empty() && frames.size() % 5 == 0;
  unsigned long previous = 0, max_gap = 0;
  size_t cycles = 0;
  const auto poll_start = RS485.polls.empty() ? millis() : RS485.polls.front().start_us / 1000;
  for (size_t i = 0; i < frames.size(); ++i) {
    frames_ok &= frames[i].id == ids[i % 5];
    if (i % 5 != 0) continue;
    max_gap = std::max(max_gap, frames[i].ms - previous);
    frames_ok &= frames[i].ms - previous >= period_ms;
    previous = frames[i].ms;
    if (previous >= poll_start) ++cycles;
  }
  max_gap = std::max(max_gap, millis() - previous);
  // One in-flight UART command and transceiver turnaround can defer a CAN cycle.
  const auto tx_budget = (std::strlen(POLL_CMDS[0]) * 10 * 1000 + RS485_BAUD - 1) / RS485_BAUD + 6;
  const bool ok = polls_ok && parses_ok && frames_ok && max_gap <= period_ms + tx_budget;
  std::printf("%s: %zu polls, %zu CAN 0x351 cycles from sweep start, max gap %lu ms [%s]\n",
              reply ? "12 replying packs" : "12 timeouts", RS485.polls.size(), cycles,
              max_gap, ok ? "PASS" : "FAIL");
  return ok;
}

bool testStartupLimits(bool late_alarm) {
  resetTest(true);
  last_pack_alarm = late_alarm;
  runSweep();
  bool stayed_blocked = true;
  for (const auto &frame : frames) {
    if (frame.id == 0x351 && frame.packs < g_bms_count) {
      stayed_blocked &= frame.charging == 0 && frame.discharging == 0;
    }
  }

  const auto end_of_sweep = millis();
  last_poll_time = end_of_sweep;
  do {
    loop();
    delay(1);
  } while (millis() - end_of_sweep < 510);
  const auto expected = late_alarm ? 0u : 100u * g_bms_count;
  bool final_limits_ok = false;
  for (const auto &frame : frames) {
    if (frame.id == 0x351 && frame.ms >= end_of_sweep) {
      final_limits_ok = frame.charging == expected && frame.discharging == expected;
    }
  }
  const auto ok = stayed_blocked && final_limits_ok;
  std::printf("Startup with %s: limits during sweep %s, after sweep %s [%s]\n",
              late_alarm ? "last pack in alarm" : "healthy packs",
              stayed_blocked ? "blocked" : "enabled too early", final_limits_ok ? "correct" : "wrong",
              ok ? "PASS" : "FAIL");
  return ok;
}

bool testDiagnosticWatchdog() {
  resetTest(false);
  constexpr unsigned long watchdog_ms = WDT_TIMEOUT * 1000ul;
  const auto start = millis();
  // Manufacturer, date, alarms, parameters, history, analog.
  constexpr size_t request_info_bytes[] = {0, 0, 1, 0, 2, 1};
  bool responses_ok = true;
  for (auto info_bytes : request_info_bytes) {
    RS485.reply = RS485.polls.size() == 5;
    const String command(std::string(18 + 2 * info_bytes, 'x').c_str());
    String response;
    responses_ok &= rs485ExchangeFrame(command, response, RS485_TIMEOUT + 200) == RS485.reply;
  }
  const auto elapsed_ms = millis() - start;
  esp_task_wdt_reset();
  const bool ok = responses_ok && elapsed_ms >= watchdog_ms && max_watchdog_gap_ms < watchdog_ms;
  std::printf("BMS diagnosis (5 timeouts, 1 reply): %lu ms total, max watchdog gap %lu/%lu ms [%s]\n",
              elapsed_ms, max_watchdog_gap_ms, watchdog_ms, ok ? "PASS" : "FAIL");
  return ok;
}

struct RefreshAge {
  unsigned long max_ms = 0;
  bool within(unsigned long limit_ms) const { return max_ms < limit_ms; }
  void measure(const std::vector<unsigned long> &refreshes, unsigned long previous = 0) {
    for (auto refresh : refreshes) {
      max_ms = std::max(max_ms, refresh - previous);
      previous = refresh;
    }
    max_ms = std::max(max_ms, millis() - previous);
  }
};

bool testRefresh(unsigned long primary_reply_ms, unsigned long aux_ms) {
  resetTest(true);
  RS485.reply_ms = primary_reply_ms;
  RS485.auxiliary_ms = aux_ms;
  simulate_auxiliary = true;
  next_alarm_poll_bms = next_sysparam_poll_bms = 0;
  for (int pack = 0; pack < g_bms_count; ++pack) {
    alarm_refreshes[pack].clear();
    sysparam_refreshes[pack].clear();
  }
  while (millis() < 900000) {
    loop();
    delay(1);
  }
  RefreshAge alarms, sysparams, telemetry;
  bool poll_order = true;
  unsigned long min_idle_ms = millis();
  std::vector<const Poll *> primary;
  for (size_t i = 0; i < RS485.polls.size(); ++i) {
    const auto &poll = RS485.polls[i];
    if (!isAuxiliary(poll.command)) primary.push_back(&poll);
    if (i > 0) {
      min_idle_ms = std::min(min_idle_ms, poll.start_us / 1000 - RS485.polls[i - 1].end_us / 1000);
    }
  }
  for (int pack = 0; pack < g_bms_count; ++pack) {
    alarms.measure(alarm_refreshes[pack]);
    sysparams.measure(sysparam_refreshes[pack]);
    std::vector<unsigned long> polls;
    for (size_t i = pack; i < primary.size(); i += g_bms_count) {
      poll_order &= primary[i]->command == POLL_CMDS[pack];
      polls.push_back(primary[i]->start_us / 1000);
    }
    telemetry.measure(polls, polls.empty() ? 0 : polls.front());
  }
  const auto tx_ms = (std::strlen(POLL_CMDS[0]) * 10 * 1000 + RS485_BAUD - 1) / RS485_BAUD;
  const auto primary_budget = g_bms_count * (BUS_GUARD_TIME + primary_reply_ms + tx_ms + 7);
  const auto auxiliary_budget = 4 * (aux_ms + BUS_GUARD_TIME + 5);
  const auto telemetry_limit = std::max(POLL_INTERVAL + 1ul, primary_budget + 1) + auxiliary_budget;
  const bool telemetry_ok = poll_order && payload_ok && telemetry.max_ms <= telemetry_limit;
  const bool guard_ok = min_idle_ms >= BUS_GUARD_TIME;
  const bool ok = alarms.within(60000) && sysparams.within(300000) && telemetry_ok && guard_ok;
  std::printf("12 packs (%lu ms telemetry reply, %lu ms auxiliary exchange): "
              "max per-pack refresh ages: alarm %lu/60000 ms, system params %lu/300000 ms; "
              "telemetry max %lu/%lu ms; RS485 min idle %lu/%u ms [%s]\n",
              primary_reply_ms, aux_ms, alarms.max_ms, sysparams.max_ms,
              telemetry.max_ms, telemetry_limit, min_idle_ms, BUS_GUARD_TIME, ok ? "PASS" : "FAIL");
  return ok;
}

int main() {
  const bool replying = testSweep(true);
  const bool timeouts = testSweep(false);
  const bool startup_healthy = testStartupLimits(false);
  const bool startup_alarm = testStartupLimits(true);
  const bool diagnosis_watchdog = testDiagnosticWatchdog();
  const bool immediate_refresh = testRefresh(0, 0);
  const bool delayed_refresh = testRefresh(150, 150);
  return replying && timeouts && startup_healthy && startup_alarm && diagnosis_watchdog
      && immediate_refresh && delayed_refresh ? 0 : 1;
}
