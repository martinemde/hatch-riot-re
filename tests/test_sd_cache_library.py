"""Compile and run the stock-sound library pager against a fixture card layout."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_local_library(tmp_path):
    host = ROOT / "tests/host/sd_cache"
    component = ROOT / "esphome/components/sd_cache"
    executable = tmp_path / "library_test"
    subprocess.run([
        shutil.which("c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-I", str(host), "-I", str(component), str(host / "library_test.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
