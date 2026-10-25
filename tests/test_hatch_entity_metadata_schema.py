"""Entity semantics for Rest+ diagnostics, validated by the installed ESPHome schemas."""
from pathlib import Path

import esphome.components
import pytest

esphome.components.__path__.append(str(Path(__file__).resolve().parents[1] / "esphome/components"))
from esphome.components.hatch_plus_power import CONFIG_SCHEMA as POWER_SCHEMA
from esphome.components.sensor import STATE_CLASSES

POWER_METADATA = {
    "battery_percent": ("mdi:battery", "battery", "%", "measurement"),
    "power_status": ("mdi:list-status", None, None, None),
    "status_74": ("mdi:chip", None, None, None),
    "status_75": ("mdi:chip", None, None, None),
    "status_76": ("mdi:chip", None, None, None),
    "external_power_present": ("mdi:power-plug", "power", None, None),
    "wireless_voltage_present": ("mdi:battery-charging-wireless", "power", None, None),
    "charge_enable_logic": ("mdi:toggle-switch-outline", None, None, None),
}


def power_config(**options):
    return {
        "hatch_expander_id": "expander",
        "acknowledge_not_power_management": True, **options,
    }


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
    result = POWER_SCHEMA(power_config(external_power_present={
        "name": "Fault", "icon": "mdi:alert", "entity_category": "", "device_class": "",
    }))
    assert result["external_power_present"]["icon"] == "mdi:alert"
    assert result["external_power_present"]["entity_category"] == ""
    assert result["external_power_present"]["device_class"] == ""


def test_metadata_does_not_create_optional_entities_or_enable_control():
    result = POWER_SCHEMA(power_config())
    assert not POWER_METADATA.keys() & result.keys()
    assert result["allow_control"] is False
