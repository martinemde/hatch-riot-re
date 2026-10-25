#include "hatch_plus.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome::hatch_plus {
static const char *const TAG = "hatch_plus";
void HatchPlus::setup() {
  preference_ = global_preferences->make_preference<FavoriteStore>(STORAGE_KEY);
  FavoriteStore loaded{};
  if (preference_.load(&loaded) && valid_store(loaded)) store_ = loaded;
  // Restoring the selection never starts audio or changes the light.
}
void HatchPlus::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch Plus: 8 favorites, URI limit 255 bytes, current %d", static_cast<int>(store_.selected));
}
bool HatchPlus::persist_() {
  const bool ok = preference_.save(&store_) && global_preferences->sync();
  persistence_dirty_ = !ok;
  if (!ok) ESP_LOGW(TAG, "Favorite preference save failed");
  return ok;
}
bool HatchPlus::configure_favorite(int index, const std::string &uri, bool repeat) {
  if (!valid_index(index) || !valid_uri(uri)) return false;
  auto &f = store_.favorites[index];
  std::memset(f.uri, 0, sizeof(f.uri));
  std::memcpy(f.uri, uri.data(), uri.size());
  f.repeat = repeat;
  f.used = 1;
  return persist_();
}
bool HatchPlus::capture(int index) {
  if (!valid_index(index)) return false;
  auto f = store_.favorites[index];
  const auto &v = light_->remote_values;
  f.light_on = v.is_on();
  f.red = v.get_red(); f.green = v.get_green(); f.blue = v.get_blue();
  f.white = v.get_white(); f.brightness = v.get_brightness();
  f.color_brightness = v.get_color_brightness();
  f.volume = player_->volume;
  f.used = 1;
  if (!valid_favorite(f)) return false;
  store_.favorites[index] = f;
  store_.selected = index;
  return persist_();
}
bool HatchPlus::play(int index) {
  if (!valid_index(index) || !store_.favorites[index].used) return false;
  const auto &f = store_.favorites[index];
  auto light_call = light_->make_call();
  light_call.set_color_mode_if_supported(light::ColorMode::RGB_WHITE).set_effect(static_cast<uint32_t>(0));
  light_call.set_state(f.light_on).set_brightness(f.brightness).set_rgb(f.red, f.green, f.blue)
      .set_white(f.white).set_color_brightness(f.color_brightness).set_transition_length(0);
  light_call.perform();
  player_->make_call().set_volume(f.volume).perform();
  player_->make_call().set_command(f.repeat ? media_player::MEDIA_PLAYER_COMMAND_REPEAT_ONE
                                          : media_player::MEDIA_PLAYER_COMMAND_REPEAT_OFF).perform();
  if (f.uri[0] != 0) player_->make_call().set_media_url(f.uri).perform();
  else player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).perform();
  if (store_.selected == index) return !persistence_dirty_ || persist_();
  store_.selected = index;
  return persist_();
}
bool HatchPlus::next() { return play(adjacent_favorite(store_, 1)); }
bool HatchPlus::previous() { return play(adjacent_favorite(store_, -1)); }
bool HatchPlus::erase(int index) {
  if (!valid_index(index)) return false;
  store_.favorites[index] = Favorite{};
  if (store_.selected == index) store_.selected = -1;
  return persist_();
}
void HatchPlus::stop() {
  light_->turn_off().perform();
  player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).perform();
}
void HatchPlus::action_(const std::string &action) {
  bool ok = true;
  if (action == "next") ok = next();
  else if (action == "previous") ok = previous();
  else if (action == "play") ok = play(store_.selected);
  else if (action == "save") ok = save_current();
  else if (action == "stop") stop();
  else if (action == "toggle_light") light_->toggle().perform();
  else if (action == "brightness_up" || action == "brightness_down") {
    float brightness = light_->remote_values.get_brightness() + (action == "brightness_up" ? 0.1f : -0.1f);
    if (brightness < 0) brightness = 0;
    if (brightness > 1) brightness = 1;
    light_->make_call().set_state(brightness > 0).set_brightness(brightness).perform();
  }
  else if (action == "volume_up") player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_VOLUME_UP).perform();
  else if (action == "volume_down") player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_VOLUME_DOWN).perform();
  if (!ok) ESP_LOGW(TAG, "Button action %s failed", action.c_str());
}
void HatchPlus::button_event(const std::string &name, bool pressed) {
  for (auto &button : buttons_) {
    if (button.name != name || button.pressed == pressed) continue;
    button.pressed = pressed;
    if (pressed) { button.pressed_at = millis(); return; }
    if (child_lock_ && !allow_when_locked_) return;
    const bool held = static_cast<uint32_t>(millis() - button.pressed_at) >= hold_ms_;
    const std::string gesture = held ? "hold" : "tap";
    action_(held ? button.hold : button.tap);
    for (auto *trigger : triggers_) trigger->trigger(name, gesture);
    return;
  }
}
}  // namespace esphome::hatch_plus
