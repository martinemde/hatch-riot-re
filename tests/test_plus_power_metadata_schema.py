"""Power and read-only probe metadata without accelerometer dependencies."""
from pathlib import Path

import esphome.components
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_plus_power import CONFIG_SCHEMA as POWER_SCHEMA
from esphome.components.hatch_plus_indicator_probe import CONFIG_SCHEMA as PROBE_SCHEMA
from esphome.components.sensor import STATE_CLASSES
from esphome.core import CORE


@pytest.fixture(autouse=True)
def reset_esphome_core():
    CORE.reset()
    yield
    CORE.reset()

POWER_METADATA = {
    "battery_percent": ("mdi:battery", "battery", "%", "measurement"),
    "battery_report_age": ("mdi:timer-outline", "duration", "s", "measurement"),
    "battery_report_accepted": ("mdi:check-circle-outline", None, None, None),
    "owner_fault": ("mdi:alert-circle-outline", "problem", None, None),
    "power_status": ("mdi:list-status", None, None, None),
    "status_74": ("mdi:chip", None, None, None),
    "status_75": ("mdi:chip", None, None, None),
    "status_76": ("mdi:chip", None, None, None),
    "external_power_present": ("mdi:power-plug", "power", None, None),
    "wireless_voltage_present": ("mdi:battery-charging-wireless", "power", None, None),
    "charge_enable_logic": ("mdi:toggle-switch-outline", None, None, None),
}


def power_config(**options):
    return {"hatch_expander_id": "expander", "i2c_id": "bus1", **options}


def assert_metadata(entity, expected):
    icon, device_class, unit, state_class = expected
    assert entity["icon"] == icon
    assert entity["entity_category"] == "diagnostic"
    assert entity.get("device_class") in (device_class, "" if device_class is None else device_class)
    assert entity.get("unit_of_measurement") in (unit, "" if unit is None else unit)
    if state_class:
        assert entity["state_class"] == STATE_CLASSES[state_class]
    else:
        assert entity.get("state_class") in (None, STATE_CLASSES[""])


@pytest.mark.parametrize("key,metadata", POWER_METADATA.items())
def test_power_entity_defaults(key, metadata):
    result = POWER_SCHEMA(power_config(**{key: {"name": key}}))
    assert_metadata(result[key], metadata)



def test_schema_defaults_can_be_overridden():
    result = POWER_SCHEMA(power_config(owner_fault={
        "name": "Fault", "icon": "mdi:alert", "entity_category": "", "device_class": "",
    }))
    assert result["owner_fault"]["icon"] == "mdi:alert"
    assert result["owner_fault"]["entity_category"] == ""
    assert result["owner_fault"]["device_class"] == ""


def test_metadata_does_not_create_optional_entities_or_enable_control():
    result = POWER_SCHEMA(power_config())
    assert not POWER_METADATA.keys() & result.keys()
    assert result["allow_control"] is False
    assert result["allow_nvm_programming"] is False
    result = PROBE_SCHEMA({"i2c_id": "bus1"})
    assert set(result) == {"id", "i2c_id", "update_interval"}
