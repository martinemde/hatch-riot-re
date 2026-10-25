"""ESPHome schema checks for button gesture timing."""
from pathlib import Path

import esphome.components
import esphome.config_validation as cv
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_plus import CONFIG_SCHEMA


def config(**options):
    return {"light_id": "night_light", "media_player_id": "player", **options}


def test_gesture_defaults_and_explicit_timings():
    result = CONFIG_SCHEMA(config())
    assert result["hold_time"].total_milliseconds == 800
    assert result["multi_click_time"].total_milliseconds == 350
    for timing in ("1ms", "2000ms"):
        result = CONFIG_SCHEMA(config(multi_click_time=timing))
        assert result["multi_click_time"].total_milliseconds in (1, 2000)


@pytest.mark.parametrize("timing", ["0ms", "2001ms", "-1ms"])
def test_multi_click_bounds(timing):
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(multi_click_time=timing))


def test_all_seven_buttons_and_unique_names():
    names = ["touch", "bottom", "night_light", "volume_up", "volume_down", "brightness_up", "brightness_down"]
    result = CONFIG_SCHEMA(config(buttons=[{"name": name} for name in names]))
    assert [button["name"] for button in result["buttons"]] == names
    with pytest.raises(cv.Invalid, match="button names must be unique"):
        CONFIG_SCHEMA(config(buttons=[{"name": "touch"}, {"name": "touch"}]))
