#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
sources = [pathlib.Path(arg) for arg in sys.argv[1:]] or sorted(root.glob("TopbandV2_03_*.ino"))
failed = False
with tempfile.TemporaryDirectory(prefix="can-cadence-") as directory:
    build = pathlib.Path(directory)
    for source in sources:
        sketch = source.read_text()
        defines = re.findall(
            r"^#define (?:BOARD_\w+|MODE_\w+|MAX_BMS|RS485_BAUD|POLL_INTERVAL|RS485_TIMEOUT|"
            r"BUS_GUARD_TIME|CAN_INTERVAL|HISTORY_INTERVAL|SD_LOG_INTERVAL|MQTT_INTERVAL|"
            r"ALARM_POLL_INTERVAL|SYSPARAM_POLL_INTERVAL|ALARM_REFRESH_TARGET|SYSPARAM_REFRESH_TARGET)\b[^\n]*",
            sketch, re.M
        )
        commands = re.search(r"^const char \*POLL_CMDS\[\] = \{[^}]+\};", sketch, re.M)[0]
        (build / "sketch_config.h").write_text("\n".join([*defines, commands]))
        functions = []
        for name in ("sendVictronCAN", "serviceCan", "rs485ExchangeFrame", "loop"):
            match = re.search(rf"^(?:static )?\w+ {name}\([^\n]*\) \{{.*?^\}}", sketch, re.M | re.S)
            if match or name != "serviceCan":  # Absent on the unfixed sketch.
                functions.append(match[0])
        (build / "sketch_paths.h").write_text("\n\n".join(functions))
        binary = build / "can_cadence"
        print(source.name, flush=True)
        subprocess.run(
            ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
             "-I", str(build), str(root / "test/can_cadence.cpp"), "-o", str(binary)], check=True
        )
        failed |= subprocess.run([str(binary)]).returncode != 0
sys.exit(failed)
