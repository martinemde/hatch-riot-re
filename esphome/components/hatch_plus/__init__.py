"""Persistent favorites, per-button tap/hold/double/triple gestures, and adjustment repeats."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light, media_player, select, switch, text
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["light", "media_player"]
HatchDisplay = cg.esphome_ns.namespace("hatch_display").class_("HatchDisplay", cg.Component)
ns = cg.esphome_ns.namespace("hatch_plus")
HatchPlus = ns.class_("HatchPlus", cg.Component)
ButtonTrigger = ns.class_("ButtonTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
ACTIONS = ["none", "next", "previous", "play", "save", "stop", "toggle_light", "brightness_up", "brightness_down", "volume_up", "volume_down"]

def unique_buttons(config):
    names = [button["name"] for button in config.get("buttons", [])]
    if len(set(names)) != len(names):
        raise cv.Invalid("button names must be unique")
    scene_buttons = [button for button in config.get("buttons", []) if button["hold"] == "manage_preset"]
    if scene_buttons and "presets" not in config:
        raise cv.Invalid("hold: manage_preset requires presets entity IDs")
    if len(scene_buttons) > 1:
        raise cv.Invalid("only one button may use hold: manage_preset")
    return config

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchPlus),
    cv.Required("light_id"): cv.use_id(light.LightState),
    cv.Required("media_player_id"): cv.use_id(media_player.MediaPlayer),
    cv.Optional("presets"): cv.Schema({
        cv.Required("stock_sound_id"): cv.use_id(select.Select),
        cv.Required("loop_switch_id"): cv.use_id(switch.Switch),
        cv.Required("clock_switch_id"): cv.use_id(switch.Switch),
        cv.Required("clock_light_id"): cv.use_id(light.LightState),
        cv.Required("text_id"): cv.use_id(text.Text),
        cv.Required("display_id"): cv.use_id(HatchDisplay),
    }),
    cv.Optional("hold_time", default="800ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=60000))),
    cv.Optional("multi_click_time", default="350ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=2000))),
    cv.Optional("allow_buttons_when_locked", default=False): cv.boolean,
    cv.Optional("buttons", default=[]): cv.ensure_list(cv.Schema({
        cv.Required("name"): cv.string_strict,
        cv.Optional("tap", default="none"): cv.one_of(*ACTIONS, lower=True),
        cv.Optional("hold", default="none"): cv.one_of(*ACTIONS, "manage_preset", lower=True),
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
    if "presets" in config:
        cg.add_define("USE_HATCH_PLUS_PRESETS")
        for key, setter in (("stock_sound_id", "set_stock_sound"), ("loop_switch_id", "set_loop_switch"),
                            ("clock_switch_id", "set_clock_switch"), ("clock_light_id", "set_clock_light"),
                            ("text_id", "set_text"), ("display_id", "set_display")):
            cg.add(getattr(var, setter)(await cg.get_variable(config["presets"][key])))
    cg.add(var.set_hold_ms(config["hold_time"].total_milliseconds))
    cg.add(var.set_multi_click_ms(config["multi_click_time"].total_milliseconds))
    cg.add(var.set_allow_when_locked(config["allow_buttons_when_locked"]))
    for button in config["buttons"]:
        cg.add(var.add_button(button["name"], button["tap"], button["hold"]))
    for conf in config.get("on_button", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "button"), (cg.std_string, "gesture")], conf)
