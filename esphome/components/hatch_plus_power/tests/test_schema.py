from pathlib import Path
import pytest
import esphome.components
import esphome.config_validation as cv

esphome.components.__path__.append(str(Path(__file__).resolve().parents[2]))
from esphome.components.hatch_plus_power import CONFIG_SCHEMA


def config(**options):
    return {"hatch_expander_id": "expander", "i2c_id": "bus1",
            "acknowledge_experimental_power_risks": True, **options}


def test_explicit_risk_ack_required():
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(acknowledge_experimental_power_risks=False))
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({"hatch_expander_id": "expander", "i2c_id": "bus1"})


def test_diagnostics_default_has_no_control_or_nvm_approval():
    result = CONFIG_SCHEMA(config())
    assert result["update_interval"].total_milliseconds == 30000
    assert result["allow_control"] is False
    assert result["allow_nvm_programming"] is False


def test_control_requires_load_and_application_dependencies():
    with pytest.raises(cv.Invalid, match="allow_control requires"):
        CONFIG_SCHEMA(config(allow_control=True))


def test_nvm_approval_is_separate_and_requires_control():
    with pytest.raises(cv.Invalid, match="allow_nvm_programming requires allow_control"):
        CONFIG_SCHEMA(config(allow_nvm_programming=True))
    control = config(allow_control=True, light_id="lamp", media_player_id="player",
                     speaker_id="spk", display_id="display", pwm_channels=[0, 1, 2, 3])
    assert CONFIG_SCHEMA(control)["allow_nvm_programming"] is False
    assert CONFIG_SCHEMA({**control, "allow_nvm_programming": True})["allow_nvm_programming"] is True


def test_unsupported_production_mode_rejected():
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA(config(production=True))
