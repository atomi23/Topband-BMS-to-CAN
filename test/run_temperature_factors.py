#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "TopbandV2_03_source.ino"
text = source.read_text()

def section(start, end):
    return text[text.index(start):text.index(end)]

def function(name):
    return re.search(rf"^\w+ {name}\([^\n]*\) \{{.*?^\}}", text, re.M | re.S).group()

with tempfile.TemporaryDirectory(prefix="temperature-factors-") as directory:
    build = pathlib.Path(directory)
    (build / "temperature_sketch.h").write_text("\n".join([
        re.search(r"^#define MAX_BMS\s+\d+", text, re.M).group(),
        section("int g_bms_count =", "struct TBFrame"),
        section("struct TBSysParam", "struct TBAlarmInfoParsed"),
        section("uint64_t g_bms_alarm_bits", "bool g_runtime_proto_uv_hit =") + "bool g_runtime_proto_uv_hit = false;",
        section("uint16_t get_u16(", "uint16_t tbBuildLengthField("),
        function("tbSelectSocValue"),
        function("parseTopband"),
        function("calcFactor"),
        function("getEffectiveChargeMinTemp"),
        function("getEffectiveDischargeMinTemp"),
        function("enforceCellBasedSafetyCaps"),
        function("calculateVictronData"),
    ]))
    binary = build / "temperature_factors"
    subprocess.run(["c++", "-std=c++11", "-I", str(build),
                    str(root / "test/temperature_factors.cpp"), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
