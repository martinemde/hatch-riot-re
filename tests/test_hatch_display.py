"""Compile and run the real display component against a timed UART fixture."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_display_scrolling(tmp_path):
    host = ROOT / "tests/host/display"
    component = ROOT / "esphome/components/hatch_display"
    executable = tmp_path / "display_test"
    subprocess.run([
        shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(host), "-I", str(component), str(host / "scroll_test.cpp"),
        str(component / "hatch_display.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
