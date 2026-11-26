#include "hatch_scenes.h"

#include <cstring>

#include "esphome/core/log.h"

namespace esphome::hatch_scenes {

static const char *const TAG = "hatch_scenes";
// Slot n is stored under PREFERENCE_KEY + n.
static constexpr uint32_t PREFERENCE_KEY = 0x48534300;  // "HSC"

void HatchScenes::setup() {
  for (int slot = 1; slot <= SLOT_COUNT; slot++) {
    auto &preference = this->preferences_[slot - 1];
    auto &scene = this->scenes_[slot - 1];
    preference = global_preferences->make_preference<Scene>(PREFERENCE_KEY + slot);
    if (!preference.load(&scene))
      scene = Scene{};
    scene.uri[URI_SIZE - 1] = '\0';
  }
  this->set_current_(0);
}

void HatchScenes::set_current_(int slot) {
  this->current_ = slot;
  if (this->current_scene_sensor_ != nullptr)
    this->current_scene_sensor_->publish_state(slot == 0 ? "None" : std::to_string(slot));
}

void HatchScenes::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch Scenes:");
  for (int slot = 1; slot <= SLOT_COUNT; slot++) {
    const auto &scene = this->scenes_[slot - 1];
    if (scene.saved)
      ESP_LOGCONFIG(TAG, "  Slot %d: light %s, %s", slot, scene.light_on ? "on" : "off",
                    scene.uri[0] ? scene.uri : "no sound");
  }
}

void HatchScenes::save() {
  const int slot = first_empty(this->scenes_);
  if (slot == 0) {
    this->show_("FULL");
    return;
  }
  this->save(slot);
}

void HatchScenes::save(int slot) {
  if (slot < 1 || slot > SLOT_COUNT) {
    ESP_LOGW(TAG, "No slot %d; slots are 1-%d", slot, SLOT_COUNT);
    this->show_("ERR");
    return;
  }
  const std::string uri = this->player_->playing_uri();
  if (uri.size() >= URI_SIZE) {
    ESP_LOGW(TAG, "The playing URI is too long to save (%zu bytes)", uri.size());
    this->show_("ERR");
    return;
  }
  const auto &light = this->light_->remote_values;
  Scene scene{};
  scene.saved = true;
  scene.light_on = light.is_on();
  scene.color_mode = static_cast<uint8_t>(light.get_color_mode());
  scene.brightness = light.get_brightness();
  scene.color_brightness = light.get_color_brightness();
  scene.red = light.get_red();
  scene.green = light.get_green();
  scene.blue = light.get_blue();
  scene.white = light.get_white();
  scene.volume = this->player_->volume;
  scene.repeat = this->player_->repeats_one();
  std::memcpy(scene.uri, uri.data(), uri.size());

  this->scenes_[slot - 1] = scene;
  this->set_current_(slot);
  this->show_(this->write_(slot) ? "SAV" + std::to_string(slot) : "ERR");
}

void HatchScenes::load(int slot) {
  if (!this->saved_(slot)) {
    this->show_("NONE");
    return;
  }
  const auto &scene = this->scenes_[slot - 1];

  auto light = this->light_->make_call();
  light.set_state(scene.light_on);
  light.set_color_mode_if_supported(static_cast<light::ColorMode>(scene.color_mode));
  light.set_brightness(scene.brightness);
  light.set_color_brightness(scene.color_brightness);
  light.set_rgb(scene.red, scene.green, scene.blue);
  light.set_white(scene.white);
  light.set_effect(static_cast<uint32_t>(0));
  light.perform();

  this->player_->make_call().set_volume(scene.volume).perform();
  this->player_->make_call()
      .set_command(scene.repeat ? media_player::MEDIA_PLAYER_COMMAND_REPEAT_ONE
                                : media_player::MEDIA_PLAYER_COMMAND_REPEAT_OFF)
      .perform();
  if (scene.uri[0] != '\0') {
    this->player_->make_call().set_media_url(scene.uri).perform();
  } else {
    this->player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).perform();
  }

  this->set_current_(slot);
  this->show_("FAV" + std::to_string(slot));
}

void HatchScenes::load_adjacent_(int step) {
  const int slot = adjacent_saved(this->scenes_, this->current_, step);
  if (slot == 0) {
    this->show_("NONE");
    return;
  }
  this->load(slot);
}

void HatchScenes::remove(int slot) {
  if (!this->saved_(slot)) {
    this->show_("NONE");
    return;
  }
  this->scenes_[slot - 1] = Scene{};
  if (this->current_ == slot)
    this->set_current_(0);
  this->show_(this->write_(slot) ? "DEL" + std::to_string(slot) : "ERR");
}

// Written through at once, not at the next flash_write_interval: a slot shown as saved survives a power cut.
bool HatchScenes::write_(int slot) {
  const bool ok = this->preferences_[slot - 1].save(&this->scenes_[slot - 1]) && global_preferences->sync();
  if (!ok)
    ESP_LOGW(TAG, "Could not write slot %d to flash", slot);
  return ok;
}

}  // namespace esphome::hatch_scenes
