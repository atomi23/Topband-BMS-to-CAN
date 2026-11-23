#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "TopbandV2_03_source.ino"
text = source.read_text()


def function(name):
    return re.search(rf"^\w[\w ]*\b{name}\(\) \{{.*?^\}}", text, re.M | re.S).group(0)


boot = re.search(r"^ *WiFiManager wm;.*?(?=^ *// 3\. Init Services)", text, re.M | re.S).group(0)
globals_area = text[:text.index("void setLed(int r, int g, int b) {")]
declarations = []
for name in sorted(set(re.findall(r"\bg_\w+", boot))):
    declaration = re.search(rf"\b(?:unsigned )?\w+ {name}\b(?: = [^;]*)?;", globals_area).group(0)
    declarations.append(declaration)
helpers = re.findall(r"^bool \w+\(struct tm \*\w+\) \{.*?^\}", text, re.M | re.S)
timestamp = re.search(r"^ *struct tm t; unsigned long ts = 0;.*$", text, re.M).group(0)

with tempfile.TemporaryDirectory(prefix="ntp-time-") as directory:
    build = pathlib.Path(directory)
    (build / "ntp_time_paths.h").write_text("\n".join([
        re.search(r"^#define MAX_BMS\s+\d+", text, re.M).group(0),
        *declarations,
        *helpers,
        function("getTimeStr"),
        function("calculateEnergy"),
        f"unsigned long dataTimestamp() {{\n{timestamp}\n  return ts;\n}}",
        f"void bootNetworkAndSettings() {{\n{boot}}}",
    ]) + "\n")
    binary = build / "ntp_time"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-I", str(build), str(root / "test/ntp_time.cpp"), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
