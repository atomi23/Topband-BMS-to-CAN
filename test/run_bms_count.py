#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "TopbandV2_03_source.ino"
text = source.read_text()
maximum = re.search(r"^#define MAX_BMS\s+(\d+)", text, re.M).group(1)
save = re.search(r'g_bms_count = ([^;]*server.arg\("cnt"\)[^;]*);', text).group(1)
load = re.search(r'g_bms_count = ([^;]*preferences.getInt\("cnt", 2\)[^;]*);', text).group(1)
with tempfile.TemporaryDirectory(prefix="bms-count-") as directory:
    build = pathlib.Path(directory)
    (build / "bms_count_paths.h").write_text(
        f"#define MAX_BMS {maximum}\n"
        f"#define SAVE_COUNT g_bms_count = {save}\n"
        f"#define LOAD_COUNT g_bms_count = {load}\n"
    )
    binary = build / "bms_count"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root), "-I", str(build),
                    str(root / "test/bms_count.cpp"), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
