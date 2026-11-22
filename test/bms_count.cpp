#include <cstdlib>
#include <iostream>
#include <string>

#include "bms_count_paths.h"

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

struct Arg {
  std::string value;
  long toInt() const { return std::strtol(value.c_str(), nullptr, 10); }
};

struct Server {
  std::string value;
  Arg arg(const char *) const { return {value}; }
} server;

struct Preferences {
  int value;
  int getInt(const char *, int) const { return value; }
} preferences;

int main() {
  struct Case { const char *input; int expected; };
  const Case cases[] = {{"21", MAX_BMS}, {"17", MAX_BMS}, {"0", 1}, {"-20", 1},
                        {"", 1}, {"invalid", 1}, {"1", 1}, {"2", 2}, {"16", 16},
                        {"2147483647", MAX_BMS}, {"-2147483648", 1}};
  int failures = 0;
  for (const Case &test : cases) {
    const char *input = test.input;
    server.value = input;
    preferences.value = static_cast<int>(server.arg("cnt").toInt());
    int expected = test.expected;
    for (bool save : {true, false}) {
      int g_bms_count = 2;
      if (save) { SAVE_COUNT; } else { LOAD_COUNT; }
      if (g_bms_count != expected) {
        std::cout << "OUT OF BOUNDS: " << (save ? "save" : "load") << " input=\"" << input
                  << "\" count=" << g_bms_count << " expected=" << expected << '\n';
        ++failures;
      }
    }
  }
  if (failures) {
    std::cout << "FAIL: " << failures << " parse/load clamp cases\n";
    return 1;
  }
  std::cout << "PASS: 22 save/load cases, including empty, malformed, bounds and integer extremes\n";
}
