from pathlib import Path
import pytest
import esphome.components
import esphome.config_validation as cv

esphome.components.__path__.append(str(Path(__file__).resolve().parents[2]))
from esphome.components.hatch_plus_power import CONFIG_SCHEMA


def test_explicit_ack_required():
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({"hatch_expander_id": "expander"})
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({"hatch_expander_id": "expander", "acknowledge_not_power_management": False})


def test_opt_in_diagnostics():
    result = CONFIG_SCHEMA({"hatch_expander_id": "expander", "acknowledge_not_power_management": True})
    assert result["update_interval"].total_milliseconds == 30000


def test_no_write_mode():
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({"hatch_expander_id": "expander", "acknowledge_not_power_management": True,
                       "production": True})
