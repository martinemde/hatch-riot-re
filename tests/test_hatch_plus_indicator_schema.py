"""Validate indicator output configuration with ESPHome."""
from pathlib import Path
from types import SimpleNamespace

import esphome.components
import esphome.config_validation as cv
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
import esphome.components.hatch_plus_indicator as indicator


def config(window="wifi", owner="power_owner", **options):
    return {"indicator": window, "hatch_plus_power_id": owner, **options}


@pytest.mark.parametrize("window,index", [("wifi", 0), ("battery", 1), ("lock", 2)])
def test_windows(window, index):
    assert indicator.CONFIG_SCHEMA(config(window))["indicator"].enum_value == index


@pytest.mark.parametrize("options", [{"indicator": "ambient"}, {"name": "Indicator"}, {"effects": []}])
def test_invalid_window_or_entity_options(options):
    with pytest.raises(cv.Invalid):
        indicator.CONFIG_SCHEMA(config(**options))


@pytest.mark.parametrize("key", ["indicator", "hatch_plus_power_id"])
def test_window_and_owner_are_required(key):
    data = config()
    del data[key]
    with pytest.raises(cv.Invalid):
        indicator.CONFIG_SCHEMA(data)


def test_one_output_per_owner_and_window(monkeypatch):
    first = indicator.CONFIG_SCHEMA(config())
    entries = [first, indicator.CONFIG_SCHEMA(config("lock")), indicator.CONFIG_SCHEMA(config(owner="another_owner"))]
    monkeypatch.setattr(indicator.fv, "full_config", SimpleNamespace(get=lambda: {"hatch_plus_indicator": entries}))
    assert indicator.FINAL_VALIDATE_SCHEMA(first) is first

    entries.append(indicator.CONFIG_SCHEMA(config("WIFI")))
    with pytest.raises(cv.Invalid, match="only one output per power owner"):
        indicator.FINAL_VALIDATE_SCHEMA(first)
