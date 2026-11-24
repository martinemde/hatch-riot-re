#pragma once

#include <initializer_list>

namespace esphome::light {
enum class ColorMode { RGB };
class LightState;

class LightTraits {
 public:
  void set_supported_color_modes(std::initializer_list<ColorMode>) {}
};

class LightOutput {
 public:
  virtual LightTraits get_traits() = 0;
  virtual void write_state(LightState *) = 0;
};
}  // namespace esphome::light
