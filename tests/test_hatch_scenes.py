"""Compile and run the scene slot order (first empty slot, next / previous with wrap-round)."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_slot_order(tmp_path):
    host = ROOT / "tests/host/hatch_scenes"
    component = ROOT / "esphome/components/hatch_scenes"
    executable = tmp_path / "slots_test"
    subprocess.run([
        shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(component), str(host / "slots_test.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
