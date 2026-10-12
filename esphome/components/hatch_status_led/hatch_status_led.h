#pragma once

#include <string>

#include "esphome/core/component.h"

namespace esphome::hatch_status_led {

enum Window : uint8_t { WINDOW_WIFI = 0, WINDOW_LOCK = 1 };
enum Colour : uint8_t { COLOUR_OFF = 0, COLOUR_GREEN = 1, COLOUR_RED = 2, COLOUR_AMBER = 3 };

class HatchStatusLed : public Component {
 public:
  void loop() override;
  void dump_config() override;
  // Run after ESPHome's own LEDC outputs: configured earlier at boot, the LEDs stayed dark on hardware.
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set(uint8_t window, uint8_t colour);
  /// Accepts "off", "green", "red" or "amber".
  void set(uint8_t window, const std::string &colour);
  uint8_t get(uint8_t window) const { return window < 2 ? this->state_[window] : COLOUR_OFF; }

 protected:
  bool init_();
  void apply_(uint8_t window);

  bool ok_{false};
  bool tried_{false};
  uint8_t state_[2]{COLOUR_OFF, COLOUR_OFF};
};

}  // namespace esphome::hatch_status_led
