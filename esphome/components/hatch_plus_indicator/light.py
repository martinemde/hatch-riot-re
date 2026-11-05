"""Discrete Plus indicator lights using the existing, safety-gated power owner."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.components.light.types import LightEffect
import esphome.final_validate as fv
from esphome.components.hatch_plus_power import HatchPlusPower
from esphome.components.hatch_plus import HatchPlus
from esphome.const import CONF_ID, CONF_OUTPUT_ID, CONF_INITIAL_STATE, CONF_EFFECT

DEPENDENCIES = ["hatch_plus_power", "wifi"]
ns = cg.esphome_ns.namespace("hatch_plus_indicator")
HatchPlusIndicator = ns.class_("HatchPlusIndicator", light.LightOutput, cg.Component)
AutomaticEffect = ns.class_("AutomaticEffect", LightEffect)
CONF_POWER = "hatch_plus_power_id"
CONF_HATCH = "hatch_plus_id"
CONF_INDICATOR = "indicator"
CONF_AUTOMATIC_ID = "automatic_effect_id"


def validate_config(config):
    if config[CONF_INDICATOR] == "lock" and CONF_HATCH not in config:
        raise cv.Invalid("lock indicator requires hatch_plus_id")
    for effect in config.get("effects", []):
        for value in effect.values():
            if isinstance(value, dict) and value.get("name", "").lower() == "automatic":
                raise cv.Invalid("Automatic is a built-in indicator effect; choose another effect name")
    return config


CONFIG_SCHEMA = cv.All(
    light.light_schema(HatchPlusIndicator, light.LightType.RGB, default_restore_mode="ALWAYS_ON")
    .extend({
        cv.Optional("entity_category", default="config"): cv.entity_category,
        cv.Optional(light.CONF_RESUME_EFFECT, default=True): cv.boolean,
        cv.Required(CONF_POWER): cv.use_id(HatchPlusPower),
        cv.Required(CONF_INDICATOR): cv.enum({"wifi": 0, "battery": 1, "lock": 2}, lower=True),
        cv.Optional(CONF_HATCH): cv.use_id(HatchPlus),
        cv.GenerateID(CONF_AUTOMATIC_ID): cv.declare_id(AutomaticEffect),
        cv.Optional(CONF_INITIAL_STATE, default={}): light.LIGHT_STATE_SCHEMA.extend({
            cv.Optional(CONF_EFFECT, default="Automatic"): cv.one_of("Automatic", "None", lower=False),
        }),
        cv.Optional("default_transition_length", default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional("gamma_correct", default=1.0): cv.positive_float,
    }).extend(cv.COMPONENT_SCHEMA),
    validate_config,
)


def final_validate(config):
    matches = [entry for entry in fv.full_config.get().get("light", [])
               if entry.get("platform") == "hatch_plus_indicator"
               and entry[CONF_POWER] == config[CONF_POWER]
               and entry[CONF_INDICATOR] == config[CONF_INDICATOR]]
    if len(matches) > 1:
        raise cv.Invalid("Each indicator may have only one light per power owner")
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    cg.add(var.set_power(await cg.get_variable(config[CONF_POWER])))
    cg.add(var.set_indicator(config[CONF_INDICATOR]))
    if CONF_HATCH in config:
        cg.add(var.set_hatch_plus(await cg.get_variable(config[CONF_HATCH])))

    initial = dict(config[CONF_INITIAL_STATE])
    automatic = initial.pop(CONF_EFFECT) == "Automatic"
    light_config = dict(config)
    light_config[CONF_INITIAL_STATE] = initial
    await light.register_light(var, light_config)
    state = await cg.get_variable(config[CONF_ID])
    effect = cg.new_Pvariable(config[CONF_AUTOMATIC_ID], var)
    cg.add(state.add_effects([effect]))
    cg.add(var.set_start_automatic(automatic))
