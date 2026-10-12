"""hatch_status_led: the two red/green status LEDs on the bottom of the Hatch Rest (RIoT), driven like stock.

Each icon window (Wi-Fi, toddler lock) is a red/green bicolour LED anti-parallel between GPIO23 and its own pin
(GPIO13 = Wi-Fi, GPIO32 = lock). One low-speed LEDC timer (12-bit, 171 Hz) holds GPIO23 at 50 %; the window pin's
duty and phase pick the colour: in phase = off, 100 % = green, 0 % = red, anti-phase = amber.
See docs/hardware-map.md#status-leds-riot. RIoT only: on RIoT+ these pins are the second I2C bus.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

hatch_status_led_ns = cg.esphome_ns.namespace("hatch_status_led")
HatchStatusLed = hatch_status_led_ns.class_("HatchStatusLed", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(HatchStatusLed)}).extend(
    cv.COMPONENT_SCHEMA
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
