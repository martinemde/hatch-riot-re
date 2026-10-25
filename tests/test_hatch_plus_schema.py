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


def test_optional_presets_require_all_entity_ids():
    assert "presets" not in CONFIG_SCHEMA(config())
    block = {"stock_sound_id": "stock", "loop_switch_id": "loop_sound", "clock_switch_id": "show_clock",
             "clock_light_id": "clock_light", "text_id": "display_text", "display_id": "display"}
    assert set(CONFIG_SCHEMA(config(presets=block))["presets"]) == set(block)
    for missing in block:
        with pytest.raises(cv.Invalid):
            CONFIG_SCHEMA(config(presets={key: value for key, value in block.items() if key != missing}))


def test_scene_gesture_uses_existing_hold_mapping():
    block = {"stock_sound_id": "stock", "loop_switch_id": "loop_sound", "clock_switch_id": "show_clock",
             "clock_light_id": "clock_light", "text_id": "display_text", "display_id": "display"}
    result = CONFIG_SCHEMA(config(presets=block, buttons=[
        {"name": "custom_touch", "tap": "next"},
        {"name": "custom_save", "tap": "toggle_light", "hold": "manage_preset"},
    ]))
    assert result["buttons"][1]["hold"] == "manage_preset"
    assert "button_controls" not in result["presets"]
    with pytest.raises(cv.Invalid, match="requires presets"):
        CONFIG_SCHEMA(config(buttons=[{"name": "custom_save", "hold": "manage_preset"}]))
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(presets=block, buttons=[{"name": "custom_save", "tap": "manage_preset"}]))

    with pytest.raises(cv.Invalid, match="only one button"):
        CONFIG_SCHEMA(config(presets=block, buttons=[
            {"name": "one", "hold": "manage_preset"}, {"name": "two", "hold": "manage_preset"},
        ]))
