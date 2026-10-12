"""hatch_expander: the Hatch Rest (RIoT) GPIO expander at 0x58, driven the way the stock firmware does.

The stock firmware uses its AW9110B driver on this chip (ID 0x23). Registers: inputs 0x00/0x01, outputs
0x02/0x03, direction 0x04/0x05 (1 = input), interrupt mask 0x06/0x07 (0 = enabled), control 0x11 (bit 4 = P0
push-pull). Pins are numbered 0-15 = P0_0..P1_7, but only the ten pins the stock firmware uses are allowed:

  P0_0-P0_4 (0-4)   buttons (inputs)       P0_5 (5)   touch controller enable
  P1_0 (8) LED enable  P1_1 (9) SD power   P1_2 (10) amp enable  P1_3 (11) ID_OUT

P0_6, P0_7 and P1_4-P1_7 are never changed (releasing P0_6 wedged the I2C bus on hardware). See
docs/hardware-map.md.

The bus is driven by a built-in bit-banged I2C master, not ESPHome's i2c component: on this board back-to-back
transactions fail often, and the IDF I2C driver sometimes hangs on a failed transaction (watchdog reset into a still
stuck bus). Bit-banging has hard timeouts everywhere and clears the bus (9 clocks + STOP) at boot and after errors.
Nothing else may use GPIO0/GPIO22 (so no `i2c:` bus on these pins).
"""
from esphome import pins
import esphome.codegen as cg
from esphome.components import gpio_expander
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_INPUT,
    CONF_INTERRUPT_PIN,
    CONF_INVERTED,
    CONF_MODE,
    CONF_NUMBER,
    CONF_OUTPUT,
    CONF_SCL,
    CONF_SDA,
)

AUTO_LOAD = ["gpio_expander"]
MULTI_CONF = True

CONF_HATCH_EXPANDER = "hatch_expander"
CONF_P0_PUSH_PULL = "p0_push_pull"

hatch_expander_ns = cg.esphome_ns.namespace("hatch_expander")
HatchExpander = hatch_expander_ns.class_("HatchExpander", cg.Component)
HatchExpanderPin = hatch_expander_ns.class_(
    "HatchExpanderPin", cg.GPIOPin, cg.Parented.template(HatchExpander)
)

# Pins the stock firmware's AW9110B pin table (0x3f4a8c18) maps to.
ALLOWED_PINS = (0, 1, 2, 3, 4, 5, 8, 9, 10, 11)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(HatchExpander),
        cv.Optional(CONF_SDA, default=0): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_SCL, default=22): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_ADDRESS, default=0x58): cv.i2c_address,
        # Bit-banged clock. Slow is fine: the expander only sees a few transactions a second.
        cv.Optional(CONF_FREQUENCY, default="50kHz"): cv.All(
            cv.frequency, cv.Range(min=1000, max=100000)
        ),
        cv.Optional(CONF_INTERRUPT_PIN): gpio_expander.validate_interrupt_pin,
        # Stock sets port 0 push-pull; the touch enable (P0_5) has no pull-up and needs it.
        cv.Optional(CONF_P0_PUSH_PULL, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_bus(config[CONF_SDA], config[CONF_SCL], config[CONF_ADDRESS], int(config[CONF_FREQUENCY])))
    cg.add(var.set_p0_push_pull(config[CONF_P0_PUSH_PULL]))
    if interrupt_pin := config.get(CONF_INTERRUPT_PIN):
        cg.add(var.set_interrupt_pin(await cg.gpio_pin_expression(interrupt_pin)))


def validate_mode(value):
    if value[CONF_INPUT] == value[CONF_OUTPUT]:
        raise cv.Invalid("Mode must be either input or output")
    return value


def validate_pin_number(value):
    value = cv.int_range(min=0, max=15)(value)
    if value not in ALLOWED_PINS:
        raise cv.Invalid(
            f"Pin {value} (P{value // 8}_{value % 8}) is not used by the stock firmware and is never "
            f"driven; allowed: {', '.join(str(p) for p in ALLOWED_PINS)}"
        )
    return value


HATCH_EXPANDER_PIN_SCHEMA = pins.gpio_base_schema(
    HatchExpanderPin,
    validate_pin_number,
    modes=[CONF_INPUT, CONF_OUTPUT],
    mode_validator=validate_mode,
).extend({cv.Required(CONF_HATCH_EXPANDER): cv.use_id(HatchExpander)})


@pins.PIN_SCHEMA_REGISTRY.register(CONF_HATCH_EXPANDER, HATCH_EXPANDER_PIN_SCHEMA)
async def hatch_expander_pin_to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    parent = await cg.get_variable(config[CONF_HATCH_EXPANDER])
    cg.add(var.set_parent(parent))
    cg.add(var.set_pin(config[CONF_NUMBER]))
    cg.add(var.set_inverted(config[CONF_INVERTED]))
    cg.add(var.set_flags(pins.gpio_flags_expr(config[CONF_MODE])))
    return var
