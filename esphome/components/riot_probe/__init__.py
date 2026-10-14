"""riot_probe: diagnostic helpers for the Hatch "riot" board.

- Detects RIoT vs RIoT+ the same way the stock firmware does (GPIO13/23/32 with pull-downs).
- Polls the GPIO expander (AW9523B/AW9110B @0x58) input registers, read only, and logs every change.
- Optionally drives one expander pin at a time. Writes need an explicit arm call, never touch pins seen
  acting as inputs, and are reverted automatically.
- Read-only register dumps of other I2C devices.
- Talks I2C by bit-banging GPIO0/GPIO22 (like hatch_expander), not through an `i2c:` bus: with PSRAM on, ESP-IDF's
  I2C driver crashed on expander writes and left the unit boot-looping until power-cycled.
- Read-only SD card probe, FAT directory listing and WAV/MP3 playback (writes are refused at the disk layer).
"""
from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import audio
from esphome.components.esp32 import include_builtin_idf_component, require_fatfs
from esphome.const import CONF_ADDRESS, CONF_FREQUENCY, CONF_ID, CONF_SCL, CONF_SDA

DEPENDENCIES = ["speaker"]

CONF_POLL_INTERVAL = "poll_interval"
CONF_WRITE_TIMEOUT = "write_timeout"
CONF_BUTTONS_AT_BOOT = "buttons_at_boot"

riot_probe_ns = cg.esphome_ns.namespace("riot_probe")
RiotProbe = riot_probe_ns.class_("RiotProbe", cg.Component)


def _request_mp3(config):
    # MP3 playback from the SD card uses the audio component's decoder.
    audio.request_mp3_support()
    return config


CONFIG_SCHEMA = cv.All(
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
            # Bit-banged bus. 50 kHz like hatch_expander (Hatch Rest).
            cv.Optional(CONF_SDA, default=0): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_SCL, default=22): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_FREQUENCY, default="50kHz"): cv.All(
                cv.frequency, cv.Range(min=1000, max=100000)
            ),
            cv.Optional(CONF_ADDRESS, default=0x58): cv.i2c_address,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _request_mp3,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_bus(
            config[CONF_SDA],
            config[CONF_SCL],
            int(config[CONF_FREQUENCY]),
            config[CONF_ADDRESS],
        )
    )
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_write_timeout(config[CONF_WRITE_TIMEOUT]))
    cg.add(var.set_buttons_at_boot(config[CONF_BUTTONS_AT_BOOT]))
    # Read-only SD card probe and FAT listing. ESPHome excludes these by default; fatfs also pulls in
    # wear_levelling and esp_driver_sdspi. require_fatfs() turns on long file names.
    for idf_component in ("esp_driver_sdmmc", "sdmmc", "esp_driver_sdspi", "wear_levelling", "fatfs"):
        include_builtin_idf_component(idf_component)
    require_fatfs()
