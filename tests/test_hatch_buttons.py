"""Compile and execute the real model-neutral gesture classifier."""
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_gestures(tmp_path):
    compiler = shutil.which("c++")
    assert compiler, "C++ compiler required"
    component = ROOT / "esphome/components/hatch_buttons"
    host = ROOT / "tests/host/hatch_buttons"
    binary = tmp_path / "gestures"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-I", str(host), "-I", str(component), str(host / "gestures_test.cpp"),
                    str(component / "hatch_buttons.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
