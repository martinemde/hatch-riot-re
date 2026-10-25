"""Independent regression cases for scene mappings and synchronous lock callbacks."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def adversarial_binary(tmp_path_factory):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path_factory.mktemp("scene_adversarial") / "scene_adversarial_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DUSE_HATCH_PLUS_PRESETS",
                    "-I", str(host), "-I", str(component),
                    str(host / "scene_adversarial_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    return exe


@pytest.mark.parametrize("scenario", ["disabled", "allow_locked", "reentrant_lock", "late_press_lock",
                                       "reentrant_lock_unlock", "late_press_lock_unlock"])
def test_scene_interactions(adversarial_binary, scenario):
    subprocess.run([str(adversarial_binary), scenario], check=True)
