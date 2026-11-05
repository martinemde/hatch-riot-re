"""Guarded DA213 sampling; read-only discovery unless explicitly enabled."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.hatch_expander import HatchExpander
from esphome.components import sensor, binary_sensor
from esphome.const import CONF_ID
DEPENDENCIES = ["hatch_expander"]
AUTO_LOAD = ["sensor", "binary_sensor"]
ns = cg.esphome_ns.namespace("hatch_accelerometer")
HatchAccelerometer = ns.class_("HatchAccelerometer", cg.PollingComponent)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchAccelerometer),
    cv.Required("hatch_expander_id"): cv.use_id(HatchExpander),
    cv.Optional("allow_initialization", default=False): cv.boolean,
    **{cv.Optional(f"acceleration_{axis}"): sensor.sensor_schema(
        unit_of_measurement="g", accuracy_decimals=3, state_class="measurement",
        icon=f"mdi:axis-{axis}-arrow", entity_category="diagnostic") for axis in ("x", "y", "z")},
    cv.Optional("tilt_angle"): sensor.sensor_schema(
        unit_of_measurement="°", accuracy_decimals=1, state_class="measurement",
        icon="mdi:angle-acute", entity_category="diagnostic"),
    cv.Optional("upright"): binary_sensor.binary_sensor_schema(
        icon="mdi:axis-z-arrow", entity_category="diagnostic"),
    cv.Optional("not_upright"): binary_sensor.binary_sensor_schema(
        icon="mdi:rotate-3d-variant", entity_category="diagnostic"),
}).extend(cv.polling_component_schema("1s"))
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_expander(await cg.get_variable(config["hatch_expander_id"])))
    cg.add(var.set_allow_initialization(config["allow_initialization"]))
    for key in ("acceleration_x", "acceleration_y", "acceleration_z", "tilt_angle"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(await sensor.new_sensor(config[key])))
    for key in ("upright", "not_upright"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(await binary_sensor.new_binary_sensor(config[key])))
