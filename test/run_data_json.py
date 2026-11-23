#!/usr/bin/env python3
import json
import pathlib
import re
import subprocess
import sys
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
sources = [pathlib.Path(path) for path in sys.argv[1:]] or sorted(root.glob("TopbandV2_03_*.ino"))
special = '"\\ä' + ''.join(map(chr, range(0x20)))


def check(source, build):
    text = source.read_text()
    parts = [
        re.search(r"^const char \*FW_VERSION = .*;$", text, re.M).group(),
        re.search(r"^String jsonEscape\(.*?^}", text, re.M | re.S).group(),
        re.search(r"^void handleData\(\) \{.*?^}", text, re.M | re.S).group(),
    ]
    (build / "data_json_paths.h").write_text("\n".join(parts) + "\n")
    binary = build / "data_json"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-parentheses",
                    "-I", str(build), str(root / "test/data_json.cpp"), "-o", str(binary)], check=True)
    baseline, mqtt, escaped = subprocess.check_output([str(binary)], text=True).split("\n", 2)
    assert json.loads(baseline)["config"]["mq_us"] == ""
    try:
        assert json.loads(mqtt)["config"]["mq_us"] == r"ACME\operator"
    except json.JSONDecodeError as exc:
        print(f"FAIL {source.name}: ACME\\operator: {exc}")
        return False
    data = json.loads(escaped)
    assert all(data["config"][key] == special for key in ("mq_us", "mq_ip", "ntp"))
    assert data["config"]["fw"] == re.search(r'"([^"]*)"', parts[0]).group(1)
    assert data["log"] == f"<div class='log-ok'>{special}</div>"
    assert data["can_status"] == special
    assert [entry["lbl"] for entry in data["bms"][0]["temps"]] == [
        "T1", "T2", "T3", "T4", "MOS", "ENV", "BAL",
    ]
    print(f"PASS {source.name}: /data JSON round-trips ACME\\operator, quotes, controls and UTF-8")
    return True


with tempfile.TemporaryDirectory(prefix="data-json-") as directory:
    sys.exit(not all([check(source, pathlib.Path(directory)) for source in sources]))
