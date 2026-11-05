#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/hatch_plus_power/hatch_plus_power.h"
#include "esphome/components/hatch_plus/hatch_plus.h"

namespace esphome::hatch_plus_indicator {

enum Indicator : uint8_t { WIFI = 0, BATTERY = 1, LOCK = 2 };

class HatchPlusIndicator : public light::LightOutput, public Component {
 public:
  void set_power(hatch_plus_power::HatchPlusPower *power) { power_ = power; }
  void set_hatch_plus(hatch_plus::HatchPlus *hatch) { hatch_ = hatch; }
  void set_indicator(uint8_t indicator) { indicator_ = indicator; }
  void setup_state(light::LightState *state) override { state_ = state; }
  void set_start_automatic(bool value) { start_automatic_ = value; }
  void set_automatic_active(bool value) {
    automatic_active_ = value;
    if (value)
      update_automatic_color();
  }
  void update_automatic_color() { automatic_desired_ = automatic_color(); }
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void loop() override;
  void dump_config() override;
  uint8_t automatic_color() const;

 protected:
  hatch_plus_power::HatchPlusPower *power_{nullptr};
  hatch_plus::HatchPlus *hatch_{nullptr};
  light::LightState *state_{nullptr};
  bool start_automatic_{false}, automatic_active_{false};
  uint8_t indicator_{WIFI}, desired_{0}, automatic_desired_{0}, applied_{0};
  bool applied_valid_{false}, attempted_{false};
  uint32_t last_attempt_{0}, last_success_{0};
};

class AutomaticEffect : public light::LightEffect {
 public:
  explicit AutomaticEffect(HatchPlusIndicator *output) : LightEffect("Automatic"), output_(output) {}
  void start() override {
    first_ = true;
    output_->set_automatic_active(true);
  }
  void stop() override { output_->set_automatic_active(false); }
  void apply() override;

 protected:
  HatchPlusIndicator *output_;
  uint32_t last_update_{0};
  bool first_{true};
};

}  // namespace esphome::hatch_plus_indicator
