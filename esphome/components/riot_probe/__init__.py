"""riot_probe: diagnostic helpers for the Hatch "riot" board.

- Detects RIoT vs RIoT+ the same way the stock firmware does (GPIO13/23/32 with pull-downs).
- Polls the GPIO expander (AW9523B/AW9110B @0x58) input registers, read only, and logs every change.
- Optionally drives one expander pin at a time. Writes need an explicit arm call, never touch pins seen
  acting as inputs, and are reverted automatically.
- Read-only register dumps of other I2C devices.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c"]

CONF_POLL_INTERVAL = "poll_interval"
CONF_WRITE_TIMEOUT = "write_timeout"
CONF_BUTTONS_AT_BOOT = "buttons_at_boot"

riot_probe_ns = cg.esphome_ns.namespace("riot_probe")
RiotProbe = riot_probe_ns.class_("RiotProbe", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RiotProbe),
            cv.Optional(
                CONF_POLL_INTERVAL, default="50ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_WRITE_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
            # Set the button pins P0_0-P0_4 to INPUT at boot (stock config) without arming. Off by default.
            cv.Optional(CONF_BUTTONS_AT_BOOT, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x58))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_write_timeout(config[CONF_WRITE_TIMEOUT]))
    cg.add(var.set_buttons_at_boot(config[CONF_BUTTONS_AT_BOOT]))
    # Read-only SD card probe (card init + ID registers, no filesystem). ESPHome excludes these by default.
    include_builtin_idf_component("esp_driver_sdmmc")
    include_builtin_idf_component("sdmmc")
