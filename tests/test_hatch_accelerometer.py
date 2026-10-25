"""Run the actual component against a recording shared-bus fixture."""
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_readonly_discovery(tmp_path):
    host=ROOT / "tests/host/accelerometer"
    component=ROOT / "esphome/components/hatch_accelerometer"
    exe=tmp_path / "probe"
    subprocess.run([shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(host), "-I", str(component), str(host/"probe_test.cpp"), str(component/"hatch_accelerometer.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
