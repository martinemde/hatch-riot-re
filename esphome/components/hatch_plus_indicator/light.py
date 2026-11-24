"""Discrete indicator lights using the existing, safety-gated power owner."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import light
from esphome.components.hatch_plus_power import HatchPlusPower
from esphome.const import (
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_EFFECTS,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_PLATFORM,
)

DEPENDENCIES = ["hatch_plus_power"]

CONF_POWER = "hatch_plus_power_id"
CONF_INDICATOR = "indicator"

ns = cg.esphome_ns.namespace("hatch_plus_indicator")
HatchPlusIndicator = ns.class_("HatchPlusIndicator", light.LightOutput, cg.Component)

CONFIG_SCHEMA = light.light_schema(
    HatchPlusIndicator, light.LightType.RGB, default_restore_mode="ALWAYS_OFF"
).extend({
    cv.Required(CONF_POWER): cv.use_id(HatchPlusPower),
    cv.Required(CONF_INDICATOR): cv.enum({"wifi": 0, "battery": 1, "lock": 2}, lower=True),
    cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
    cv.Optional(CONF_EFFECTS): cv.All(cv.ensure_list(), cv.Length(max=0)),
}).extend(cv.COMPONENT_SCHEMA)


def final_validate(config):
    matches = [
        entry for entry in fv.full_config.get().get("light", [])
        if entry.get(CONF_PLATFORM) == "hatch_plus_indicator"
        and entry[CONF_POWER] == config[CONF_POWER]
        and entry[CONF_INDICATOR] == config[CONF_INDICATOR]
    ]
    if len(matches) > 1:
        raise cv.Invalid("Each indicator may have only one light per power owner")
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    cg.add(var.set_power(await cg.get_variable(config[CONF_POWER])))
    cg.add(var.set_indicator(config[CONF_INDICATOR]))
    await light.register_light(var, config)
