"""Run the actual component against a recording shared-bus fixture."""
from pathlib import Path
import shutil
import subprocess
import esphome.components

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_accelerometer import CONFIG_SCHEMA

ROOT = Path(__file__).resolve().parents[1]


def test_da213_driver(tmp_path):
    host = ROOT / "tests/host/accelerometer"
    component = ROOT / "esphome/components/hatch_accelerometer"
    executable = tmp_path / "accelerometer"
    subprocess.run([
        shutil.which("c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(host), "-I", str(component), str(host / "probe_test.cpp"),
        str(component / "hatch_accelerometer.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)


def test_sampling_and_configured_entities_enabled_by_default():
    result = CONFIG_SCHEMA({
        "hatch_expander_id": "io_exp",
        "tilt_angle": {"name": "Tilt Angle"},
        "upright": {"name": "Upright"},
        "acceleration_x": {"name": "Acceleration X"},
        "acceleration_y": {"name": "Acceleration Y"},
        "acceleration_z": {"name": "Acceleration Z"},
    })
    assert result["allow_initialization"] is True
    for key in ("tilt_angle", "upright", "acceleration_x", "acceleration_y", "acceleration_z"):
        assert result[key]["disabled_by_default"] is False
    assert "not_upright" not in result


def test_explicit_read_only_and_disabled_entity():
    result = CONFIG_SCHEMA({
        "hatch_expander_id": "io_exp",
        "allow_initialization": False,
        "tilt_angle": {"name": "Disabled Tilt Angle", "disabled_by_default": True},
    })
    assert result["allow_initialization"] is False
    assert result["tilt_angle"]["disabled_by_default"] is True
    assert "upright" not in result and "acceleration_x" not in result
