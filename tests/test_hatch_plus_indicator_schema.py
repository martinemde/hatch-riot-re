"""Validate discrete indicator light configuration with ESPHome."""
from itertools import count
from pathlib import Path
from types import SimpleNamespace

import esphome.components
import esphome.config_validation as cv
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_plus_indicator import light as platform


_entity_names = count()


def config(indicator="wifi", owner="power_owner", **options):
    return {"name": f"Test Indicator {next(_entity_names)}", "indicator": indicator,
            "hatch_plus_power_id": owner, **options}


@pytest.mark.parametrize("indicator,index", [("wifi", 0), ("battery", 1), ("lock", 2)])
def test_manual_defaults(indicator, index):
    result = platform.CONFIG_SCHEMA(config(indicator))
    assert result["indicator"].enum_value == index
    assert result["restore_mode"] == "ALWAYS_OFF"
    assert result["default_transition_length"].total_milliseconds == 0
    assert result["gamma_correct"] == 1
    assert not result.get("effects")


@pytest.mark.parametrize("options", [
    {"indicator": "ambient"},
    {"hatch_plus_id": "controller"},
    {"effects": [{"pulse": {"name": "Pulse"}}]},
    {"initial_state": {"effect": "Automatic"}},
])
def test_invalid_selector_or_policy_options(options):
    with pytest.raises(cv.Invalid):
        platform.CONFIG_SCHEMA(config(**options))


@pytest.mark.parametrize("key", ["indicator", "hatch_plus_power_id"])
def test_selector_and_owner_are_required(key):
    data = config()
    del data[key]
    with pytest.raises(cv.Invalid):
        platform.CONFIG_SCHEMA(data)


def test_one_light_per_owner_and_window(monkeypatch):
    def entry(**options):
        return {"platform": "hatch_plus_indicator", **platform.CONFIG_SCHEMA(config(**options))}

    light = entry()
    lights = [light, entry(indicator="lock"), entry(owner="another_owner"), {"platform": "rgb"}]
    monkeypatch.setattr(platform.fv, "full_config", SimpleNamespace(get=lambda: {"light": lights}))
    assert platform.FINAL_VALIDATE_SCHEMA(light) is light

    lights.append(entry(indicator="WIFI", name="Duplicate window"))
    with pytest.raises(cv.Invalid, match="only one light per power owner"):
        platform.FINAL_VALIDATE_SCHEMA(light)
