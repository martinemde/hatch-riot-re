"""Compile actual read-only component against a recording native-bus interface."""
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
def test_readonly_probe(tmp_path):
    host = ROOT / "tests/host/indicator_probe"
    component = ROOT / "esphome/components/hatch_plus_indicator_probe"
    exe = tmp_path / "probe"
    subprocess.run([shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(host), "-I", str(component), str(host/"probe_test.cpp"), str(component/"hatch_plus_indicator_probe.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
