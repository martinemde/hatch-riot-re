#pragma once

#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::hatch_plus_power {
class HatchPlusPower;
}

namespace esphome::hatch_plus_indicator {

// Each window is a red/green LED with no dimming; yellow is both colours on.
enum class Color : uint8_t { OFF, RED, YELLOW, GREEN };

class HatchPlusIndicator : public Component {
 public:
  void set_power(hatch_plus_power::HatchPlusPower *power) { power_ = power; }
  void set_indicator(uint8_t indicator) { indicator_ = indicator; }
  void set_color(Color color) { desired_ = color; }
  void loop() override;
  void dump_config() override;

 private:
  hatch_plus_power::HatchPlusPower *power_{nullptr};
  uint8_t indicator_{0};
  Color desired_{Color::OFF}, applied_{Color::OFF};
  bool applied_valid_{false}, attempted_{false};
  uint32_t last_attempt_{0}, last_success_{0};
};

}  // namespace esphome::hatch_plus_indicator
