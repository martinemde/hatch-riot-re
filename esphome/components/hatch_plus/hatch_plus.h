#pragma once
#include "favorites.h"
#include "esphome/components/light/light_state.h"
#ifdef USE_HATCH_PLUS_PRESETS
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/hatch_display/hatch_display.h"
#endif
#include "esphome/components/media_player/media_player.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <vector>

namespace esphome {
namespace select { class Select; }
namespace switch_ { class Switch; }
namespace text { class Text; }
namespace hatch_display { class HatchDisplay; }
}
namespace esphome::hatch_plus {
struct Button {
  std::string name, tap, hold;
  bool pressed{false};
  uint32_t pressed_at{0}, last_repeat_at{0}, released_at{0};
  uint8_t clicks{0};
  bool repeating{false}, cancelled{false};
  bool scene_hold{false}, delete_preview{false};
  int delete_target{-1};
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
  void set_child_lock(bool locked);
  bool get_child_lock() const { return child_lock_; }
  void add_button(const std::string &name, const std::string &tap, const std::string &hold) {
    buttons_.push_back({name, tap, hold});
  }
  void add_button_trigger(Trigger<std::string, std::string> *trigger) { triggers_.push_back(trigger); }
  void set_stock_sound(select::Select *value) { stock_sound_ = value; }
  void set_loop_switch(switch_::Switch *value) { loop_switch_ = value; }
  void set_clock_switch(switch_::Switch *value) { clock_switch_ = value; }
  void set_clock_light(light::LightState *value) { clock_light_ = value; }
  void set_text(text::Text *value) { text_ = value; }
  void set_display(hatch_display::HatchDisplay *value) { display_ = value; }
  bool store_preset(int index, const std::string &name);
  bool apply_preset(int index);
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
  bool presets_ready_() const;
  bool scene_controls_ready_() const;
  void preview_scene_delete_(Button &button);
  void show_scene_feedback_(const std::string &message);
  void finish_scene_hold_(Button &button, uint32_t duration);
  void snapshot_light_(Favorite &favorite) const;
  void restore_light_(const Favorite &favorite);
  void action_(const std::string &action);
  void finish_clicks_(Button &button);
  void emit_(const Button &button, const std::string &gesture);
  light::LightState *light_{nullptr};
  media_player::MediaPlayer *player_{nullptr};
  select::Select *stock_sound_{nullptr};
  switch_::Switch *loop_switch_{nullptr}, *clock_switch_{nullptr};
  light::LightState *clock_light_{nullptr};
  text::Text *text_{nullptr};
  hatch_display::HatchDisplay *display_{nullptr};
  ESPPreferenceObject preference_;
  FavoriteStore store_{};
  std::vector<Button> buttons_;
  std::vector<Trigger<std::string, std::string> *> triggers_;
  static constexpr uint32_t REPEAT_INTERVAL_MS = 150;
  uint32_t hold_ms_{800}, multi_click_ms_{350};
  static constexpr uint32_t SCENE_SAVE_MS = 5000, SCENE_DELETE_MS = 10000;
  bool child_lock_{false}, allow_when_locked_{false};
  bool persistence_dirty_{false};
};
class ButtonTrigger : public Trigger<std::string, std::string> {
 public:
  explicit ButtonTrigger(HatchPlus *parent) { parent->add_button_trigger(this); }
};
}  // namespace esphome::hatch_plus
