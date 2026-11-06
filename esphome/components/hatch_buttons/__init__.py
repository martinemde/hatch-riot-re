"""Model-neutral named-button gesture classification."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID

ns = cg.esphome_ns.namespace("hatch_buttons")
HatchButtons = ns.class_("HatchButtons", cg.Component)
ButtonTrigger = ns.class_("ButtonTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
PressTrigger = ns.class_("PressTrigger", automation.Trigger.template(cg.std_string))
ReleaseTrigger = ns.class_("ReleaseTrigger", automation.Trigger.template(cg.std_string, cg.uint32))
CancelTrigger = ns.class_("CancelTrigger", automation.Trigger.template(cg.std_string))


def unique_buttons(config):
    names = [button["name"] for button in config["buttons"]]
    if len(set(names)) != len(names):
        raise cv.Invalid("button names must be unique")
    return config


CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchButtons),
    cv.Required("buttons"): cv.ensure_list(cv.Schema({cv.Required("name"): cv.All(cv.string_strict, cv.Length(min=1))})),
    cv.Optional("hold_time", default="800ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=60000))),
    cv.Optional("multi_click_time", default="350ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=2000))),
    cv.Optional("allow_buttons_when_locked", default=False): cv.boolean,
    cv.Optional("on_button"): automation.validate_automation({cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ButtonTrigger)}),
    cv.Optional("on_press"): automation.validate_automation({cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PressTrigger)}),
    cv.Optional("on_release"): automation.validate_automation({cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ReleaseTrigger)}),
    cv.Optional("on_cancel"): automation.validate_automation({cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CancelTrigger)}),
}).extend(cv.COMPONENT_SCHEMA), unique_buttons)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_hold_ms(config["hold_time"].total_milliseconds))
    cg.add(var.set_multi_click_ms(config["multi_click_time"].total_milliseconds))
    cg.add(var.set_allow_when_locked(config["allow_buttons_when_locked"]))
    for button in config["buttons"]:
        cg.add(var.add_button(button["name"]))
    for key, args in (
        ("on_button", [(cg.std_string, "button"), (cg.std_string, "gesture")]),
        ("on_press", [(cg.std_string, "button")]),
        ("on_release", [(cg.std_string, "button"), (cg.uint32, "duration")]),
        ("on_cancel", [(cg.std_string, "button")]),
    ):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, args, conf)
