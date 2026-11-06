#pragma once
#include "favorites.h"
#include "esphome/components/light/light_state.h"
#ifdef USE_HATCH_SCENES_PRESETS
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/hatch_display/hatch_display.h"
#endif
#include "esphome/components/media_player/media_player.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace select { class Select; }
namespace switch_ { class Switch; }
namespace text { class Text; }
namespace hatch_display { class HatchDisplay; }
}
namespace esphome::hatch_scenes {
class HatchScenes : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_light(light::LightState *light) { light_ = light; }
  void set_media_player(media_player::MediaPlayer *player) { player_ = player; }
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
  void management_press();
  void management_release(uint32_t duration);
  void management_cancel();
 protected:
  bool persist_();
  bool presets_ready_() const;
  void preview_scene_delete_();
  void show_scene_feedback_(const std::string &message);
  void snapshot_light_(Favorite &favorite) const;
  void restore_light_(const Favorite &favorite);
  light::LightState *light_{nullptr};
  media_player::MediaPlayer *player_{nullptr};
  select::Select *stock_sound_{nullptr};
  switch_::Switch *loop_switch_{nullptr}, *clock_switch_{nullptr};
  light::LightState *clock_light_{nullptr};
  text::Text *text_{nullptr};
  hatch_display::HatchDisplay *display_{nullptr};
  ESPPreferenceObject preference_;
  FavoriteStore store_{};
  static constexpr uint32_t SCENE_SAVE_MS = 5000, SCENE_DELETE_MS = 10000;
  bool management_pressed_{false}, delete_preview_{false};
  uint32_t management_pressed_at_{0};
  int delete_target_{-1};
  bool persistence_dirty_{false};
};
}  // namespace esphome::hatch_scenes
