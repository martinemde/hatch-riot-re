"""Native tests for offline byte adapters, never live I2C."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_register_transport(tmp_path):
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler
    executable = tmp_path / "register_transport"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                    str(ROOT / "tests/host/power_register_transport_test.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_greenpak_nvm_programming_requires_separate_approval(tmp_path):
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler
    executable = tmp_path / "greenpak_boot_transport"
    component = ROOT / "esphome/components/hatch_plus_power"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(component),
                    str(component / "tests/greenpak_boot_transport_test.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_power_status_flags(tmp_path):
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler
    executable = tmp_path / "power_status"
    source = ROOT / "esphome/components/hatch_plus_power/tests/power_status_test.cpp.test"
    subprocess.run([compiler, "-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
