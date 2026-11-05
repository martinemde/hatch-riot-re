"""Exercise the actual indicator LightOutput against a recording power owner."""
from itertools import count
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def indicator_binary(tmp_path_factory):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/plus_indicator"
    component = ROOT / "esphome/components/hatch_plus_indicator"
    exe = tmp_path_factory.mktemp("plus_indicator") / "indicator_test"
    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-I", str(host), "-I", str(component),
         str(host / "indicator_test.cpp"),
         str(component / "hatch_plus_indicator.cpp"), "-o", str(exe)],
        check=True,
    )
    return exe


@pytest.mark.parametrize("scenario", ["battery", "wifi", "manual", "blinking", "explicit_off", "local_status", "retry", "boot"])
def test_indicator(indicator_binary, scenario):
    subprocess.run([str(indicator_binary), scenario], check=True)


@pytest.fixture(scope="module")
def indicator_schema():
    import esphome.components

    esphome.components.__path__.append(str(ROOT / "esphome/components"))
    from esphome.components.hatch_plus_indicator import light

    return light


_entity_names = count()


def indicator_config(indicator="wifi", **options):
    return {"name": f"Bottom indicator {next(_entity_names)}", "indicator": indicator,
            "hatch_plus_power_id": "power_owner", **options}


@pytest.mark.parametrize("indicator", ["wifi", "battery", "lock"])
def test_schema_default_automatic(indicator_schema, indicator):
    options = {"hatch_plus_id": "controller"} if indicator == "lock" else {}
    result = indicator_schema.CONFIG_SCHEMA(indicator_config(indicator, **options))
    assert result["initial_state"]["effect"] == "Automatic"
    assert result["restore_mode"] == "ALWAYS_ON"
    assert result["default_transition_length"].total_milliseconds == 0
    assert result["gamma_correct"] == 1
    assert result["resume_effect"] is True


def test_schema_lock_requires_controller(indicator_schema):
    import esphome.config_validation as cv

    with pytest.raises(cv.Invalid, match="lock indicator requires hatch_plus_id"):
        indicator_schema.CONFIG_SCHEMA(indicator_config("lock"))


def test_schema_manual_initial_state_with_effect(indicator_schema):
    result = indicator_schema.CONFIG_SCHEMA(indicator_config(
        initial_state={"effect": "None", "state": False, "red": 1, "green": 0},
        effects=[{"pulse": {"name": "Pulse"}}],
    ))
    assert result["initial_state"]["effect"] == "None"
    assert result["initial_state"]["state"] is False
    assert result["effects"][0]["pulse"]["name"] == "Pulse"


def test_schema_unique_indicator_per_owner(indicator_schema, monkeypatch):
    from types import SimpleNamespace
    import esphome.config_validation as cv

    def entry(owner="power_owner", indicator="wifi"):
        return {"platform": "hatch_plus_indicator", **indicator_schema.CONFIG_SCHEMA(
            indicator_config(indicator, hatch_plus_power_id=owner))}

    config = entry()
    full_config = {"light": [config, entry(indicator="battery"), entry(owner="other_owner"),
                             {"platform": "rgb"}]}
    monkeypatch.setattr(indicator_schema.fv, "full_config", SimpleNamespace(get=lambda: full_config))
    assert indicator_schema.FINAL_VALIDATE_SCHEMA(config) is config
    full_config["light"].append(entry())
    with pytest.raises(cv.Invalid, match="only one light per power owner"):
        indicator_schema.FINAL_VALIDATE_SCHEMA(config)
