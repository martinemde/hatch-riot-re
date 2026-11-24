"""DA213/MSA311 tilt sensing on the Hatch expander's shared I2C bus."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor
from esphome.components.hatch_expander import HatchExpander
from esphome.const import CONF_ID, CONF_MODEL

DEPENDENCIES = ["hatch_expander"]
AUTO_LOAD = ["sensor", "binary_sensor"]

CONF_HATCH_EXPANDER_ID = "hatch_expander_id"
CONF_ALLOW_INITIALIZATION = "allow_initialization"
AXIS_KEYS = {f"acceleration_{axis}": axis for axis in ("x", "y", "z")}

ns = cg.esphome_ns.namespace("hatch_accelerometer")
HatchAccelerometer = ns.class_("HatchAccelerometer", cg.PollingComponent)

AccelerometerModel = ns.enum("AccelerometerModel", is_class=True)
MODELS = {
    "AUTO": AccelerometerModel.AUTO,
    "DA213": AccelerometerModel.DA213,
    "MSA311": AccelerometerModel.MSA311,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HatchAccelerometer),
        cv.Required(CONF_HATCH_EXPANDER_ID): cv.use_id(HatchExpander),
        cv.Optional(CONF_MODEL, default="AUTO"): cv.enum(MODELS, upper=True),
        cv.Optional(CONF_ALLOW_INITIALIZATION, default=True): cv.boolean,
        **{
            cv.Optional(key): sensor.sensor_schema(
                unit_of_measurement="g",
                accuracy_decimals=3,
                state_class="measurement",
                icon=f"mdi:axis-{axis}-arrow",
                entity_category="diagnostic",
            )
            for key, axis in AXIS_KEYS.items()
        },
        cv.Optional("tilt_angle"): sensor.sensor_schema(
            unit_of_measurement="°",
            accuracy_decimals=1,
            state_class="measurement",
            icon="mdi:angle-acute",
            entity_category="diagnostic",
        ),
        cv.Optional("upright"): binary_sensor.binary_sensor_schema(
            icon="mdi:axis-z-arrow", entity_category="diagnostic"
        ),
        cv.Optional("not_upright"): binary_sensor.binary_sensor_schema(
            icon="mdi:rotate-3d-variant", entity_category="diagnostic"
        ),
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_expander(await cg.get_variable(config[CONF_HATCH_EXPANDER_ID])))
    cg.add(var.set_model(config[CONF_MODEL]))
    cg.add(var.set_allow_initialization(config[CONF_ALLOW_INITIALIZATION]))
    for key in (*AXIS_KEYS, "tilt_angle"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(await sensor.new_sensor(config[key])))
    for key in ("upright", "not_upright"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(await binary_sensor.new_binary_sensor(config[key])))
