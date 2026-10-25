#pragma once
#include "favorites.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <vector>

namespace esphome::hatch_plus {
struct Button {
  std::string name, tap, hold;
  bool pressed{false};
  uint32_t pressed_at{0}, last_repeat_at{0}, released_at{0};
  uint8_t clicks{0};
  bool repeating{false};
};
class HatchPlus : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_light(light::LightState *light) { light_ = light; }
  void set_media_player(media_player::MediaPlayer *player) { player_ = player; }
  void set_hold_ms(uint32_t ms) { hold_ms_ = ms; }
  void set_multi_click_ms(uint32_t ms) { multi_click_ms_ = ms; }
  void set_allow_when_locked(bool allow) { allow_when_locked_ = allow; }
  void set_child_lock(bool locked) {
    child_lock_ = locked;
    if (locked && !allow_when_locked_) {
      for (auto &button : buttons_) button.clicks = 0;
    }
  }
  bool get_child_lock() const { return child_lock_; }
  void add_button(const std::string &name, const std::string &tap, const std::string &hold) {
    buttons_.push_back({name, tap, hold});
  }
  void add_button_trigger(Trigger<std::string, std::string> *trigger) { triggers_.push_back(trigger); }
  bool configure_favorite(int index, const std::string &uri, bool repeat);
  bool capture(int index);
  bool save_current() { return capture(store_.selected); }
  bool play(int index);
  bool next();
  bool previous();
  bool erase(int index);
  int current_favorite() const { return store_.selected; }
  void stop();
  void button_event(const std::string &name, bool pressed);
 protected:
  bool persist_();
  void action_(const std::string &action);
  void finish_clicks_(Button &button);
  void emit_(const Button &button, const std::string &gesture);
  light::LightState *light_{nullptr};
  media_player::MediaPlayer *player_{nullptr};
  ESPPreferenceObject preference_;
  FavoriteStore store_{};
  std::vector<Button> buttons_;
  std::vector<Trigger<std::string, std::string> *> triggers_;
  static constexpr uint32_t REPEAT_INTERVAL_MS = 150;
  uint32_t hold_ms_{800}, multi_click_ms_{350};
  bool child_lock_{false}, allow_when_locked_{false};
  bool persistence_dirty_{false};
};
class ButtonTrigger : public Trigger<std::string, std::string> {
 public:
  explicit ButtonTrigger(HatchPlus *parent) { parent->add_button_trigger(this); }
};
}  // namespace esphome::hatch_plus
