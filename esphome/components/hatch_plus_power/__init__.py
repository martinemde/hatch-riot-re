"""Experimental RIoT+ diagnostics and opt-in power control; not safety validated."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, light, media_player, speaker, i2c
from esphome.components.hatch_display import HatchDisplay
from esphome.components.esp32 import include_builtin_idf_component
from esphome.components.hatch_expander import HatchExpander
from esphome.const import CONF_ID

DEPENDENCIES = ["hatch_expander", "i2c"]
AUTO_LOAD = ["sensor", "binary_sensor"]
CONF_EXPANDER = "hatch_expander_id"
CONF_ACK = "acknowledge_experimental_power_risks"
ns = cg.esphome_ns.namespace("hatch_plus_power")
HatchPlusPower = ns.class_("HatchPlusPower", cg.PollingComponent)


def require_ack(value):
    value = cv.boolean(value)
    if not value:
        raise cv.Invalid("Experimental control manages charging, thermal policy and shutdown; NVM programming needs separate approval. Hardware safety and recovery are unverified; explicit true acknowledgment required")
    return value


def validate_control(config):
    if config["allow_nvm_programming"] and not config["allow_control"]:
        raise cv.Invalid("allow_nvm_programming requires allow_control")
    if config["allow_control"]:
        missing = [key for key in ("light_id", "media_player_id", "display_id", "speaker_id", "pwm_channels") if key not in config]
        if missing:
            raise cv.Invalid("allow_control requires " + ", ".join(missing))
    return config


CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlusPower),
    cv.Required(CONF_EXPANDER): cv.use_id(HatchExpander),
    cv.Required("i2c_id"): cv.use_id(i2c.I2CBus),
    cv.Required(CONF_ACK): require_ack,
    cv.Optional("allow_control", default=False): cv.boolean,
    cv.Optional("allow_nvm_programming", default=False): cv.boolean,
    cv.Optional("pwm_channels"): cv.All(cv.ensure_list(cv.int_range(min=0, max=15)), cv.Length(min=4, max=4)),
    cv.Optional("light_id"): cv.use_id(light.LightState),
    cv.Optional("media_player_id"): cv.use_id(media_player.MediaPlayer),
    cv.Optional("display_id"): cv.use_id(HatchDisplay),
    cv.Optional("speaker_id"): cv.use_id(speaker.Speaker),
    cv.Optional("battery_percent"): sensor.sensor_schema(
        unit_of_measurement="%", accuracy_decimals=0, device_class="battery",
        state_class="measurement", icon="mdi:battery", entity_category="diagnostic"),
    cv.Optional("battery_report_age"): sensor.sensor_schema(
        unit_of_measurement="s", accuracy_decimals=1, device_class="duration",
        state_class="measurement", icon="mdi:timer-outline", entity_category="diagnostic"),
    cv.Optional("battery_report_accepted"): binary_sensor.binary_sensor_schema(
        icon="mdi:check-circle-outline", entity_category="diagnostic"),
    cv.Optional("owner_fault"): binary_sensor.binary_sensor_schema(
        device_class="problem", icon="mdi:alert-circle-outline", entity_category="diagnostic"),
    cv.Optional("power_status"): sensor.sensor_schema(
        accuracy_decimals=0, icon="mdi:list-status", entity_category="diagnostic"),
    cv.Optional("status_74"): sensor.sensor_schema(
        accuracy_decimals=0, icon="mdi:chip", entity_category="diagnostic"),
    cv.Optional("status_75"): sensor.sensor_schema(
        accuracy_decimals=0, icon="mdi:chip", entity_category="diagnostic"),
    cv.Optional("status_76"): sensor.sensor_schema(
        accuracy_decimals=0, icon="mdi:chip", entity_category="diagnostic"),
    cv.Optional("external_power_present"): binary_sensor.binary_sensor_schema(
        device_class="power", icon="mdi:power-plug", entity_category="diagnostic"),
    cv.Optional("wireless_voltage_present"): binary_sensor.binary_sensor_schema(
        device_class="power", icon="mdi:battery-charging-wireless", entity_category="diagnostic"),
    cv.Optional("charge_enable_logic"): binary_sensor.binary_sensor_schema(
        icon="mdi:toggle-switch-outline", entity_category="diagnostic"),
}).extend(cv.polling_component_schema("30s")), validate_control)


async def to_code(config):
    include_builtin_idf_component("driver")
    include_builtin_idf_component("esp_adc")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_expander(await cg.get_variable(config[CONF_EXPANDER])))
    cg.add(var.set_i2c_bus(await cg.get_variable(config["i2c_id"])))
    cg.add(var.set_allow_control(config["allow_control"]))
    cg.add(var.set_allow_nvm_programming(config["allow_nvm_programming"]))
    if "pwm_channels" in config:
        cg.add(var.set_pwm_channels(config["pwm_channels"]))
    for key, setter in (("light_id", "set_light"), ("media_player_id", "set_player"), ("display_id", "set_display"), ("speaker_id", "set_speaker")):
        if key in config:
            cg.add(getattr(var, setter)(await cg.get_variable(config[key])))
    for key in ("battery_percent", "battery_report_age", "power_status", "status_74", "status_75", "status_76"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await sensor.new_sensor(config[key])))
    for key in ("external_power_present", "wireless_voltage_present", "charge_enable_logic", "battery_report_accepted", "owner_fault"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await binary_sensor.new_binary_sensor(config[key])))
