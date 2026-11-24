"""Run the indicator LightOutput against a recording power owner."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_indicator_output(tmp_path):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/plus_indicator"
    component = ROOT / "esphome/components/hatch_plus_indicator"
    exe = tmp_path / "indicator_test"
    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-I", str(host), "-I", str(component),
         str(host / "indicator_test.cpp"),
         str(component / "hatch_plus_indicator.cpp"), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
