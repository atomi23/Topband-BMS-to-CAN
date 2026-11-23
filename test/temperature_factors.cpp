#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <string>

using std::abs;
using std::fabs;
using std::max;
using std::min;
using std::time;

struct String : std::string {
  using std::string::string;
  String(int value) : std::string(std::to_string(value)) {}
};

unsigned long millis() { return 120000; }
void addToLog(const std::string &, bool) {}
void saveAutoBalanceTimestamp(uint32_t) { std::abort(); }
uint64_t tbFilterCriticalAlarmBits(uint64_t, float, float, int, float, float, float, float) { std::abort(); }
bool tbShouldFlagProtoUnderVolt(float, float, int) { std::abort(); }
struct TBSysParam;
void tbBuildAutoCfgSuggestion(const TBSysParam &, float, float, float, float,
                             float &, float &, float &, float &, float &, float &, float &, float &, float &) { std::abort(); }

#include "temperature_sketch.h"

void parsePack(int index, std::initializer_list<int> temperatures) {
  String frame = "D07C0000";
  char hex[5];
  std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned>(temperatures.size()));
  frame += hex;
  for (int temperature : temperatures) {
    std::snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(2731 + 10 * temperature));
    frame += hex;
  }
  frame += "0000138813880027100000"; // 50 V, 50/100 Ah
  parseTopband(frame, index);
}

int failures = 0;

void expect(const char *label, float actual, float expected) {
  bool ok = std::fabs(actual - expected) < 0.0001f;
  std::printf("%s %s: %.2f (expected %.2f)\n", ok ? "PASS" : "FAIL", label, actual, expected);
  if (!ok) failures++;
}

int main() {
  g_auto_balance_enable = false;
  g_bms_count = 2;
  parsePack(0, {-5});
  parsePack(1, {25});
  calculateVictronData();
  expect("MAX SAFETY charge factor (-5, 25)", factor_charge, 0.0f);
  std::swap(bms[0], bms[1]);
  calculateVictronData();
  expect("MAX SAFETY charge factor (25, -5)", factor_charge, 0.0f);

  g_temp_mode = 1;
  calculateVictronData();
  expect("AVERAGE charge factor (25, -5)", factor_charge, 1.0f);
  std::swap(bms[0], bms[1]);
  calculateVictronData();
  expect("AVERAGE charge factor (-5, 25)", factor_charge, 1.0f);

  parsePack(1, {10, 10, 10});
  calculateVictronData();
  expect("AVERAGE temperature with unequal sensor counts", victronData.avgTemp, 6.25f);
  expect("AVERAGE charge factor with unequal sensor counts", factor_charge, 0.2f);

  parsePack(0, {-25, 25, 25});
  bms[1].last_seen = 0;
  g_temp_mode = 0;
  calculateVictronData();
  expect("MAX SAFETY charge factor with a cold sensor", factor_charge, 0.0f);
  expect("MAX SAFETY discharge factor with a cold sensor", factor_discharge, 0.0f);
  expect("Pack sensor mean", bms[0].avgTemp, 25.0f / 3.0f);
  g_temp_mode = 1;
  calculateVictronData();
  expect("AVERAGE charge factor with a stale pack", factor_charge, 0.2f);

  parsePack(0, {7, 49});
  g_temp_mode = 0;
  calculateVictronData();
  expect("MAX SAFETY chooses stricter soft derating", factor_charge, 0.2f);
  parsePack(0, {25, 65});
  calculateVictronData();
  expect("MAX SAFETY charge factor with a hot sensor", factor_charge, 0.0f);
  expect("MAX SAFETY discharge factor with a hot sensor", factor_discharge, 0.0f);
  return failures != 0;
}
