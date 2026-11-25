"""Numbered scenes: save the night light and sound, then play them back."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light, text_sensor
from esphome.components.hatch_display import HatchDisplay
from esphome.components.speaker_source.media_player import SpeakerSourceMediaPlayer
from esphome.const import CONF_ID

DEPENDENCIES = ["hatch_display", "light", "media_player"]
AUTO_LOAD = ["text_sensor"]

SLOT_COUNT = 9  # Matches SLOT_COUNT in scene.h.

ns = cg.esphome_ns.namespace("hatch_scenes")
HatchScenes = ns.class_("HatchScenes", cg.Component)
SaveAction = ns.class_("SaveAction", automation.Action, cg.Parented.template(HatchScenes))
LoadAction = ns.class_("LoadAction", automation.Action, cg.Parented.template(HatchScenes))
NextAction = ns.class_("NextAction", automation.Action, cg.Parented.template(HatchScenes))
PreviousAction = ns.class_("PreviousAction", automation.Action, cg.Parented.template(HatchScenes))
DeleteAction = ns.class_("DeleteAction", automation.Action, cg.Parented.template(HatchScenes))

CONF_SLOT = "slot"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HatchScenes),
    cv.Required("light_id"): cv.use_id(light.LightState),
    cv.Required("media_player_id"): cv.use_id(SpeakerSourceMediaPlayer),
    cv.Required("display_id"): cv.use_id(HatchDisplay),
    # The slot loaded or saved last, as text: a numeric sensor would show "1.0".
    cv.Optional("current_scene"): text_sensor.text_sensor_schema(
        icon="mdi:star", entity_category="diagnostic"),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_light(await cg.get_variable(config["light_id"])))
    cg.add(var.set_player(await cg.get_variable(config["media_player_id"])))
    cg.add(var.set_display(await cg.get_variable(config["display_id"])))
    if "current_scene" in config:
        cg.add(var.set_current_scene_sensor(await text_sensor.new_text_sensor(config["current_scene"])))


SLOT = cv.templatable(cv.int_range(min=1, max=SLOT_COUNT))
OPTIONAL_SLOT_SCHEMA = automation.maybe_simple_id({
    cv.GenerateID(): cv.use_id(HatchScenes),
    cv.Optional(CONF_SLOT): SLOT,
})
NO_SLOT_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(HatchScenes)})
LOAD_SCHEMA = cv.maybe_simple_value({
    cv.GenerateID(): cv.use_id(HatchScenes),
    cv.Required(CONF_SLOT): SLOT,
}, key=CONF_SLOT)


@automation.register_action("hatch_scenes.save", SaveAction, OPTIONAL_SLOT_SCHEMA, synchronous=True)
@automation.register_action("hatch_scenes.load", LoadAction, LOAD_SCHEMA, synchronous=True)
@automation.register_action("hatch_scenes.next", NextAction, NO_SLOT_SCHEMA, synchronous=True)
@automation.register_action("hatch_scenes.previous", PreviousAction, NO_SLOT_SCHEMA, synchronous=True)
@automation.register_action("hatch_scenes.delete", DeleteAction, OPTIONAL_SLOT_SCHEMA, synchronous=True)
async def scene_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_SLOT in config:
        cg.add(var.set_slot(await cg.templatable(config[CONF_SLOT], args, cg.int_)))
    return var
