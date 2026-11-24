#pragma once

namespace esphome::light {
struct LightColorValues {
  bool on = false;
  float brightness = 1, color_brightness = 1, red = 1, green = 1, blue = 1;

  bool is_on() const { return on; }
  float get_brightness() const { return brightness; }
  float get_color_brightness() const { return color_brightness; }
  float get_red() const { return red; }
  float get_green() const { return green; }
};

class LightState {
 public:
  LightColorValues current_values;
};
}  // namespace esphome::light
