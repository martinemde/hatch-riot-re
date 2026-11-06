"""Persistent light/audio favorites and optional clock/text scene snapshots."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, media_player, select, switch, text
from esphome.const import CONF_ID

DEPENDENCIES = ["light", "media_player"]
HatchDisplay = cg.esphome_ns.namespace("hatch_display").class_("HatchDisplay", cg.Component)
ns = cg.esphome_ns.namespace("hatch_scenes")
HatchScenes = ns.class_("HatchScenes", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchScenes),
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
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_light(await cg.get_variable(config["light_id"])))
    cg.add(var.set_media_player(await cg.get_variable(config["media_player_id"])))
    if "presets" in config:
        cg.add_define("USE_HATCH_SCENES_PRESETS")
        for key, setter in (("stock_sound_id", "set_stock_sound"), ("loop_switch_id", "set_loop_switch"),
                            ("clock_switch_id", "set_clock_switch"), ("clock_light_id", "set_clock_light"),
                            ("text_id", "set_text"), ("display_id", "set_display")):
            cg.add(getattr(var, setter)(await cg.get_variable(config["presets"][key])))
