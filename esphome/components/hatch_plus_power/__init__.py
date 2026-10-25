"""Experimental read-only RIoT+ GreenPAK diagnostics, NOT power management."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, light, media_player, speaker
from esphome.components.hatch_display import HatchDisplay
from esphome.components.esp32 import include_builtin_idf_component
from esphome.components.hatch_expander import HatchExpander
from esphome.const import CONF_ID

DEPENDENCIES = ["hatch_expander"]
AUTO_LOAD = ["sensor", "binary_sensor"]
CONF_EXPANDER = "hatch_expander_id"
CONF_ACK = "acknowledge_not_power_management"
ns = cg.esphome_ns.namespace("hatch_plus_power")
HatchPlusPower = ns.class_("HatchPlusPower", cg.PollingComponent)


def require_ack(value):
    value = cv.boolean(value)
    if not value:
        raise cv.Invalid("This diagnostic component does NOT manage charging, thermal limits or shutdown; explicit true acknowledgment required")
    return value


def validate_control(config):
    if config["allow_control"]:
        missing = [key for key in ("light_id", "media_player_id", "display_id", "speaker_id", "pwm_channels") if key not in config]
        if missing:
            raise cv.Invalid("allow_control requires " + ", ".join(missing))
    return config


CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlusPower),
    cv.Required(CONF_EXPANDER): cv.use_id(HatchExpander),
    cv.Required(CONF_ACK): require_ack,
    cv.Optional("allow_control", default=False): cv.boolean,
    cv.Optional("pwm_channels"): cv.All(cv.ensure_list(cv.int_range(min=0, max=15)), cv.Length(min=4, max=4)),
    cv.Optional("light_id"): cv.use_id(light.LightState),
    cv.Optional("media_player_id"): cv.use_id(media_player.MediaPlayer),
    cv.Optional("display_id"): cv.use_id(HatchDisplay),
    cv.Optional("speaker_id"): cv.use_id(speaker.Speaker),
    cv.Optional("battery_percent"): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=0),
    cv.Optional("power_status"): sensor.sensor_schema(accuracy_decimals=0),
    cv.Optional("status_74"): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic"),
    cv.Optional("status_75"): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic"),
    cv.Optional("status_76"): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic"),
    cv.Optional("external_power_present"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("wireless_voltage_present"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("charge_enable_logic"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
}).extend(cv.polling_component_schema("30s")), validate_control)


async def to_code(config):
    include_builtin_idf_component("driver")
    include_builtin_idf_component("esp_adc")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_expander(await cg.get_variable(config[CONF_EXPANDER])))
    cg.add(var.set_allow_control(config["allow_control"]))
    if "pwm_channels" in config:
        cg.add(var.set_pwm_channels(config["pwm_channels"]))
    for key, setter in (("light_id", "set_light"), ("media_player_id", "set_player"), ("display_id", "set_display"), ("speaker_id", "set_speaker")):
        if key in config:
            cg.add(getattr(var, setter)(await cg.get_variable(config[key])))
    for key in ("battery_percent", "power_status", "status_74", "status_75", "status_76"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await sensor.new_sensor(config[key])))
    for key in ("external_power_present", "wireless_voltage_present", "charge_enable_logic"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await binary_sensor.new_binary_sensor(config[key])))
