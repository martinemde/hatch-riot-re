#pragma once

#include <cstdint>

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"

namespace esphome::hatch_plus_power {
class HatchPlusPower;
}

namespace esphome::hatch_plus_indicator {

class HatchPlusIndicator : public light::LightOutput, public Component {
 public:
  void set_power(hatch_plus_power::HatchPlusPower *power) { power_ = power; }
  void set_indicator(uint8_t indicator) { indicator_ = indicator; }
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void loop() override;
  void dump_config() override;

 private:
  // State values accepted by HatchPlusPower::set_indicator().
  enum class Color : uint8_t { OFF = 0, GREEN = 1, RED = 2, YELLOW = 3 };

  hatch_plus_power::HatchPlusPower *power_{nullptr};
  uint8_t indicator_{0};
  Color desired_{Color::OFF}, applied_{Color::OFF};
  bool applied_valid_{false}, attempted_{false};
  uint32_t last_attempt_{0}, last_success_{0};
};

}  // namespace esphome::hatch_plus_indicator
