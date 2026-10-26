"""Native regressions for immediate adjustment taps and unchanged hold/lock behavior."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def adjustment_binary(tmp_path_factory):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path_factory.mktemp("adjustment_taps") / "adjustment_taps_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(host), "-I", str(component),
                    str(host / "adjustment_taps_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    return exe


@pytest.mark.parametrize("scenario", ["rapid", "repeat", "explicit_hold", "lock"])
def test_adjustment_taps(adjustment_binary, scenario):
    subprocess.run([str(adjustment_binary), scenario], check=True)
