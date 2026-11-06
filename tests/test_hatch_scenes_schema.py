"""Required scene entities and optional complete preset wiring."""
from pathlib import Path

import esphome.components
import esphome.config_validation as cv
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_scenes import CONFIG_SCHEMA


def test_required_entities_and_optional_presets():
    config = {"light_id": "night_light", "media_player_id": "player"}
    assert "presets" not in CONFIG_SCHEMA(config)
    for missing in config:
        with pytest.raises(cv.Invalid):
            CONFIG_SCHEMA({key: value for key, value in config.items() if key != missing})
    presets = {"stock_sound_id": "stock", "loop_switch_id": "loop_sound", "clock_switch_id": "show_clock",
               "clock_light_id": "clock_light", "text_id": "display_text", "display_id": "display"}
    assert set(CONFIG_SCHEMA({**config, "presets": presets})["presets"]) == set(presets)
    for missing in presets:
        with pytest.raises(cv.Invalid):
            CONFIG_SCHEMA({**config, "presets": {key: value for key, value in presets.items() if key != missing}})


def test_gesture_policy_not_in_scenes_schema():
    config = {"light_id": "night_light", "media_player_id": "player"}
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({**config, "buttons": [{"name": "touch", "tap": "next"}]})
