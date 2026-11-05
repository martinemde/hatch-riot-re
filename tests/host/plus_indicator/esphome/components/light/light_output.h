#pragma once
#include <initializer_list>
#include "light_state.h"
namespace esphome::light {
enum class ColorMode { RGB };
class LightTraits {
 public:
  void set_supported_color_modes(std::initializer_list<ColorMode>) {}
};
class LightOutput {
 public:
  virtual ~LightOutput() = default;
  virtual LightTraits get_traits() = 0;
  virtual void setup_state(LightState *) {}
  virtual void write_state(LightState *) = 0;
};
}
