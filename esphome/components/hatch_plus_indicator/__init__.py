"""Off/red/yellow/green outputs for the Hatch Rest+ bottom indicator windows."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components.hatch_plus_power import HatchPlusPower
from esphome.const import CONF_ID

DEPENDENCIES = ["hatch_plus_power"]
MULTI_CONF = True

CONF_DOMAIN = "hatch_plus_indicator"
CONF_POWER = "hatch_plus_power_id"
CONF_INDICATOR = "indicator"

ns = cg.esphome_ns.namespace("hatch_plus_indicator")
HatchPlusIndicator = ns.class_("HatchPlusIndicator", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlusIndicator),
    cv.Required(CONF_POWER): cv.use_id(HatchPlusPower),
    cv.Required(CONF_INDICATOR): cv.enum({"wifi": 0, "battery": 1, "lock": 2}, lower=True),
}).extend(cv.COMPONENT_SCHEMA)


def final_validate(config):
    matches = [
        entry for entry in fv.full_config.get().get(CONF_DOMAIN, [])
        if entry[CONF_POWER] == config[CONF_POWER] and entry[CONF_INDICATOR] == config[CONF_INDICATOR]
    ]
    if len(matches) > 1:
        raise cv.Invalid("Each indicator may have only one output per power owner")
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_power(await cg.get_variable(config[CONF_POWER])))
    cg.add(var.set_indicator(config[CONF_INDICATOR]))
