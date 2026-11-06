"""ESPHome schema checks for the shared button classifier."""
from pathlib import Path
import esphome.components
import esphome.config_validation as cv
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_buttons import CONFIG_SCHEMA


def config(**options):
    return {"buttons": [{"name": "touch"}], **options}


def test_defaults_and_custom_names():
    result = CONFIG_SCHEMA(config())
    assert result["hold_time"].total_milliseconds == 800
    assert result["multi_click_time"].total_milliseconds == 350
    assert not result["allow_buttons_when_locked"]
    assert CONFIG_SCHEMA(config(buttons=[{"name": "custom"}]))["buttons"][0]["name"] == "custom"


@pytest.mark.parametrize("key,value", [("hold_time", "0ms"), ("hold_time", "60001ms"),
    ("multi_click_time", "0ms"), ("multi_click_time", "2001ms"), ("multi_click_time", "-1ms")])
def test_bounds(key, value):
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(**{key: value}))


@pytest.mark.parametrize("key,value", [("hold_time", "1ms"), ("hold_time", "60000ms"),
    ("multi_click_time", "1ms"), ("multi_click_time", "2000ms")])
def test_valid_bounds(key, value):
    assert CONFIG_SCHEMA(config(**{key: value}))[key].total_milliseconds > 0


def test_unique_nonempty_names_and_no_native_actions():
    with pytest.raises(cv.Invalid, match="button names must be unique"):
        CONFIG_SCHEMA(config(buttons=[{"name": "touch"}, {"name": "touch"}]))
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(buttons=[{"name": ""}]))
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(buttons=[{"name": "touch", "tap": "next"}]))
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(light_id="night_light"))
