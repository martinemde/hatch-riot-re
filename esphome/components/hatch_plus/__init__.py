"""Persistent local light/audio favorites and release-classified button gestures."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light, media_player
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["light", "media_player"]
ns = cg.esphome_ns.namespace("hatch_plus")
HatchPlus = ns.class_("HatchPlus", cg.Component)
ButtonTrigger = ns.class_("ButtonTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
ACTIONS = ["none", "next", "previous", "play", "save", "stop", "toggle_light", "brightness_up", "brightness_down", "volume_up", "volume_down"]

def unique_buttons(config):
    names = [button["name"] for button in config.get("buttons", [])]
    if len(set(names)) != len(names):
        raise cv.Invalid("button names must be unique")
    return config

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlus),
    cv.Required("light_id"): cv.use_id(light.LightState),
    cv.Required("media_player_id"): cv.use_id(media_player.MediaPlayer),
    cv.Optional("hold_time", default="800ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=60000))),
    cv.Optional("allow_buttons_when_locked", default=False): cv.boolean,
    cv.Optional("buttons", default=[]): cv.ensure_list(cv.Schema({
        cv.Required("name"): cv.string_strict,
        cv.Optional("tap", default="none"): cv.one_of(*ACTIONS, lower=True),
        cv.Optional("hold", default="none"): cv.one_of(*ACTIONS, lower=True),
    })),
    cv.Optional("on_button"): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ButtonTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA), unique_buttons)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_light(await cg.get_variable(config["light_id"])))
    cg.add(var.set_media_player(await cg.get_variable(config["media_player_id"])))
    cg.add(var.set_hold_ms(config["hold_time"].total_milliseconds))
    cg.add(var.set_allow_when_locked(config["allow_buttons_when_locked"]))
    for button in config["buttons"]:
        cg.add(var.add_button(button["name"], button["tap"], button["hold"]))
    for conf in config.get("on_button", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "button"), (cg.std_string, "gesture")], conf)
