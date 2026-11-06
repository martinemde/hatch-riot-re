"""Validate the Hatch Rest / Rest+ classified-button blueprint with an existing Home Assistant environment.

Run with the Home Assistant environment's Python; no dependency installation:
    /path/to/homeassistant/.venv/bin/python tests/validate_hatch_buttons_blueprint.py
"""

import asyncio
import logging
from pathlib import Path
from tempfile import TemporaryDirectory

from homeassistant import loader
from homeassistant.components.automation.config import (
    AUTOMATION_BLUEPRINT_SCHEMA,
    PLATFORM_SCHEMA,
)
from homeassistant.components.blueprint.models import Blueprint, BlueprintInputs
from homeassistant.components.homeassistant.triggers import event, state
from homeassistant.const import __version__
from homeassistant.core import Context, HomeAssistant
from homeassistant.helpers import condition, script, trigger
from homeassistant.util import yaml

ROOT = Path(__file__).resolve().parents[1]
BUTTONS = ["touch", "volume_up", "volume_down", "brightness_up", "brightness_down", "night_light", "bottom"]
GESTURES = ["tap", "hold", "double", "triple"]
ACTION_KEYS = [f"{button}_{gesture}_action" for button in BUTTONS for gesture in GESTURES]


def load_blueprint(path):
    blueprint = Blueprint(
        yaml.load_yaml_dict(ROOT / "blueprints/automation" / path),
        expected_domain="automation",
        schema=AUTOMATION_BLUEPRINT_SCHEMA,
    )
    assert blueprint.validate() is None
    return blueprint


def expand(blueprint, inputs):
    instance = BlueprintInputs(blueprint, {"use_blueprint": {"input": inputs}})
    instance.validate()
    config = PLATFORM_SCHEMA(instance.async_substitute())
    for item in config["triggers"]:
        if item["platform"] == "event":
            event.TRIGGER_SCHEMA(item)
        else:
            state.TRIGGER_STATE_SCHEMA(item)
    return config


async def validate(config_dir):
    hass = HomeAssistant(config_dir)
    buttons = load_blueprint("hatch/buttons.yaml")
    assert set(buttons.inputs) == {"event_device_name", "lock_switch", *ACTION_KEYS}
    assert all(buttons.inputs[key]["default"] == [] for key in ACTION_KEYS)
    custom = {
        key: [{"event": "hatch_test_action", "event_data": {"key": key}}]
        for key in ACTION_KEYS
    }
    expand(buttons, {})
    expand(buttons, custom)
    print(f"PASS HA {__version__}: button blueprint/automation/trigger schemas; 30 inputs; 28 empty/custom action defaults")

    loader.async_setup(hass)
    await trigger.async_setup(hass)
    action_events = []
    service_calls = []
    hass.bus.async_listen("hatch_test_action", lambda event: action_events.append(event.data["key"]))

    async def record_service(call):
        service_calls.append((call.domain, call.service, call.data))

    for domain, service in [
        ("scene", "turn_on"), ("script", "turn_on"),
        ("light", "turn_off"), ("media_player", "media_stop"),
    ]:
        hass.services.async_register(domain, service, record_service)

    async def make_automation(blueprint, inputs):
        config = expand(blueprint, inputs)
        config["triggers"] = await trigger.async_validate_trigger_config(hass, config["triggers"])
        config["actions"] = await script.async_validate_actions_config(hass, config["actions"])
        checker = await condition.async_conditions_from_config(
            hass, config["conditions"], logging.getLogger(__name__), "hatch"
        )
        runner = script.Script(hass, config["actions"], "hatch", "automation", variables=config["variables"])

        async def run(variables, context=None):
            variables = config["variables"].async_render(hass, variables)
            if checker(variables):
                await runner.async_run(variables, context or Context())

        remove = await trigger.async_initialize_triggers(
            hass, config["triggers"], run, "automation", "hatch",
            lambda *args, **kwargs: None, variables={},
        )
        assert remove is not None
        return remove

    async def fire(device="hatch-rest", button="touch", gesture="tap"):
        hass.bus.async_fire("esphome.hatch_button", {"device": device, "button": button, "gesture": gesture})
        await hass.async_block_till_done()

    async def tap():
        hass.states.async_set("binary_sensor.hatch_touch", "on")
        hass.states.async_set("binary_sensor.hatch_touch", "off")
        await hass.async_block_till_done()

    remove = await make_automation(buttons, custom)
    hass.states.async_set("binary_sensor.hatch_touch", "off")
    await tap()
    await fire(device="another-device")
    await fire(button="unknown")
    await fire(gesture="unknown")
    invalid_payloads = [{}, {"device": "hatch-rest"}, {"device": "hatch-rest", "button": "touch"}]
    for field in ["device", "button", "gesture"]:
        for value in [None, 17, [], {}, True]:
            invalid_payloads.append({"device": "hatch-rest", "button": "touch", "gesture": "tap", field: value})
    for data in invalid_payloads:
        hass.bus.async_fire("esphome.hatch_button", data)
    hass.bus.async_fire("esphome.another_event", {"device": "hatch-rest", "button": "touch", "gesture": "tap"})
    await hass.async_block_till_done()
    assert action_events == [] and service_calls == []
    for button in BUTTONS:
        for gesture in GESTURES:
            await fire(button=button, gesture=gesture)
    assert action_events == ACTION_KEYS, action_events
    assert service_calls == [], service_calls
    remove()

    remove = await make_automation(buttons, {})
    before_actions = list(action_events)
    for button in BUTTONS:
        for gesture in GESTURES:
            await fire(button=button, gesture=gesture)
    await tap()
    assert action_events == before_actions and service_calls == []
    remove()

    for lock_entity in ["switch.lock", "input_boolean.lock"]:
        remove = await make_automation(buttons, {
            "event_device_name": "nursery", "lock_switch": lock_entity, **custom,
        })
        action_events.clear()
        await fire()
        assert action_events == []
        hass.states.async_set(lock_entity, "on")
        await fire(device="nursery")
        assert action_events == []
        hass.states.async_set(lock_entity, "off")
        await fire(device="nursery")
        assert action_events == ["touch_tap_action"]
        remove()
    print("PASS Rest / Rest+ runtime: all 28 routes/default no-ops; device filter; missing/unknown fields; raw sensor isolation; switch/input_boolean lock")

    await hass.async_stop()


if __name__ == "__main__":
    with TemporaryDirectory(prefix="hatch-blueprint-ha-") as config_dir:
        asyncio.run(validate(config_dir))
