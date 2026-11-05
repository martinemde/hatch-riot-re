"""Compile offline charge and composed task cores without ESPHome or a bus adapter."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_standalone_power_controller(tmp_path):
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler, "a C++17 compiler is required for the standalone power core"
    executable = tmp_path / "power_controller_test"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT), str(ROOT / "tests/host/power_controller_test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
