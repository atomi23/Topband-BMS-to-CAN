#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "TopbandV2_03_source.ino"
text = source.read_text()


def extract(start, end):
    return re.search(r"^" + start + r"[\s\S]*?^" + end, text, re.M)[0].rsplit("\n", 1)[0]


sections = [
    re.search(r"^#define MAX_BMS\s+\d+", text, re.M)[0],
    extract("int g_bms_count =", "unsigned long last_poll_time"),
    extract("bool can_error_flag", "struct TBFrame"),
    extract("struct TBSysParam", "struct TBAlarmInfoParsed"),
    extract("uint64_t g_bms_alarm_bits", "// --- HELPERS ---"),
    extract("const uint64_t TB_CRITICAL_ALARM_MASK", "bool tbHasCriticalAlarm"),
    extract("void tbBuildAutoCfgSuggestion", "bool tbSystemParamPlausible"),
    extract("float calcFactor", "void saveAutoBalanceTimestamp"),
    extract("void calculateVictronData", "void sendCanFrame"),
    re.search(r"^void sendVictronCAN\(\) \{[\s\S]*?^\}", text, re.M)[0],
]
with tempfile.TemporaryDirectory(prefix="charge-block-") as directory:
    build = pathlib.Path(directory)
    (build / "charge_block_sketch.h").write_text("\n".join(sections) + "\n")
    binary = build / "charge_block"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-misleading-indentation", "-I", str(build),
                    str(root / "test/charge_block.cpp"), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
