#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

using String = std::string;
using std::min;

unsigned long millis() { return 1000; }
void delay(unsigned long) {}
void addToLog(const String &, bool) {}
void saveAutoBalanceTimestamp(uint32_t) {}
time_t testTime(time_t *now) { return *now = 1800000000; }

uint8_t limits_frame[8];
bool sent_limits;
void sendCanFrame(uint32_t id, uint8_t *data) {
  if (id == 0x351) {
    std::memcpy(limits_frame, data, sizeof limits_frame);
    sent_limits = true;
  }
}

#define time testTime
#include "charge_block_sketch.h"
#undef time

int main() {
  struct Case {
    const char *name;
    int soc;
    float voltage;
    float temp;
    uint64_t bms_alarm;
    bool maintenance;
    bool auto_balance;
    uint8_t flags;
    int ccl;
    int dcl;
  };
  const Case cases[] = {
      {"99% SOC, pack overvoltage",          99, 54, 25, 0,          false, false, 0x02,  0,   0},
      {"99% SOC, BMS undervoltage",          99, 48, 25, 1ULL << 12, false, true,  0x10,  0,   0},
      {"99% SOC, critical BMS status",       99, 50, 25, 1ULL << 4,  false, true,  0x40,  0,   0},
      {"100% SOC, critical BMS status",     100, 50, 25, 1ULL << 4,  false, true,  0x40,  0,   0},
      {"99% SOC, too cold to charge",        99, 50,  0, 0,          false, true,  0x08,  0, 300},
      {"99% SOC, too hot to charge",         99, 50, 53, 0,          false, true,  0x08,  0, 300},
      {"99% SOC, overvoltage, maintenance",  99, 54, 25, 0,          true,  true,  0x02,  0,   0},
      {"99% SOC, healthy trickle",           99, 50, 25, 0,          false, false, 0x00, 20, 300},
      {"100% SOC, healthy auto-balance",    100, 50, 25, 0,          false, true,  0x00, 20, 300},
  };

  g_bms_count = 1;
  bms[0].valid = true;
  bms[0].last_seen = millis();
  bms[0].cell_count = 15;
  bms[0].temp_count = 1;
  bms[0].minCellV = bms[0].maxCellV = 3.35f;
  int failures = 0;
  for (const Case &test : cases) {
    bms[0].soc = test.soc;
    bms[0].voltage = test.voltage;
    bms[0].temps[0] = bms[0].maxTemp = bms[0].avgTemp = test.temp;
    g_bms_alarm_bits[0] = test.bms_alarm;
    g_bms_alarm_last_seen[0] = millis();
    g_maint_charge_mode = test.maintenance;
    g_auto_balance_enable = test.auto_balance;
    g_auto_balance_last_ts = 0;
    sent_limits = false;

    calculateVictronData();
    sendVictronCAN();

    const int ccl = limits_frame[2] | (limits_frame[3] << 8);
    const int dcl = limits_frame[4] | (limits_frame[5] << 8);
    const bool pass = sent_limits && ccl == test.ccl && dcl == test.dcl && alarm_flags == test.flags;
    if (!pass) ++failures;
    std::cout << (pass ? "PASS: " : "FAIL: ") << test.name << " -> CAN 0x351 CCL "
              << std::fixed << std::setprecision(1) << ccl / 10.0 << " A, DCL " << dcl / 10.0
              << " A (expected " << test.ccl / 10.0 << "/" << test.dcl / 10.0 << " A)\n";
  }
  return failures != 0;
}
