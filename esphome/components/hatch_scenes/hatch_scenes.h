#pragma once
#include <string>

#include "scene.h"
#include "esphome/components/hatch_display/hatch_display.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/speaker_source/speaker_source_media_player.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::hatch_scenes {

/// Saves the night light and what the speaker plays into numbered slots, and plays them back.
/// Every call confirms itself on the clock: SAVn, FAVn, DELn, FULL, NONE, or ERR.
class HatchScenes : public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_light(light::LightState *light) { this->light_ = light; }
  void set_player(speaker_source::SpeakerSourceMediaPlayer *player) { this->player_ = player; }
  void set_display(hatch_display::HatchDisplay *display) { this->display_ = display; }
  void set_current_scene_sensor(text_sensor::TextSensor *sensor) { this->current_scene_sensor_ = sensor; }

  /// Save into the first empty slot.
  void save();
  /// Save into `slot`, replacing what was there.
  void save(int slot);
  void load(int slot);
  void next() { this->load_adjacent_(1); }
  void previous() { this->load_adjacent_(-1); }
  /// Delete the slot loaded or saved last.
  void remove() { this->remove(this->current_); }
  void remove(int slot);
  /// The slot loaded or saved last, or 0. Not kept across a restart.
  int current_slot() const { return this->current_; }

 protected:
  bool saved_(int slot) const { return slot >= 1 && slot <= SLOT_COUNT && this->scenes_[slot - 1].saved; }
  void load_adjacent_(int step);
  bool write_(int slot);
  void show_(const std::string &text) { this->display_->show_text_overlay(text); }
  void set_current_(int slot);

  light::LightState *light_{nullptr};
  speaker_source::SpeakerSourceMediaPlayer *player_{nullptr};
  hatch_display::HatchDisplay *display_{nullptr};
  text_sensor::TextSensor *current_scene_sensor_{nullptr};
  Scenes scenes_{};
  ESPPreferenceObject preferences_[SLOT_COUNT];
  int current_{0};
};

}  // namespace esphome::hatch_scenes
