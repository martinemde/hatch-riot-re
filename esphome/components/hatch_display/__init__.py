"""hatch_display: the Hatch Rest 4-digit clock, driven over UART1 like the stock firmware.

The display has its own MCU; the ESP32 only sends frames (19200 8O1, TX GPIO27). See docs/display-protocol.md:
  08 S0 S1 S2 S3 M   digits (stock font) + misc (bit 0 = centre colon)
  18 B               brightness, from a 16-entry table: level 0 = brightest, 15 = off
The display stays dark until it receives a brightness frame, so one is sent at setup.
"""
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_BRIGHTNESS, CONF_ID

DEPENDENCIES = ["uart"]

hatch_display_ns = cg.esphome_ns.namespace("hatch_display")
HatchDisplay = hatch_display_ns.class_("HatchDisplay", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HatchDisplay),
            # 0 = brightest, 15 = off (the display's own scale).
            cv.Optional(CONF_BRIGHTNESS, default=4): cv.int_range(min=0, max=15),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "hatch_display", baud_rate=19200, data_bits=8, parity="ODD", stop_bits=1, require_tx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_initial_brightness(config[CONF_BRIGHTNESS]))
