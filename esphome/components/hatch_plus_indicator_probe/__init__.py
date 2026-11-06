"""Read-only Plus bus1 discovery; no indicator or power control."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_I2C_ID
DEPENDENCIES = ["i2c"]
ns = cg.esphome_ns.namespace("hatch_plus_indicator_probe")
HatchPlusIndicatorProbe = ns.class_("HatchPlusIndicatorProbe", cg.PollingComponent)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlusIndicatorProbe),
    cv.Required(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
}).extend(cv.polling_component_schema("30s"))
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_i2c_bus(await cg.get_variable(config[CONF_I2C_ID])))
