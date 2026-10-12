#pragma once

#include <string>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome::hatch_display {

class HatchDisplay : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_initial_brightness(uint8_t level) { this->brightness_ = level; }

  /// Show up to 4 characters, right-aligned ('.' after a character lights that digit's decimal point).
  void print(const std::string &text);
  /// Show text (as print()) with the colon off.
  void show_text(const std::string &text);
  /// Show HH:MM (or H:MM) with the colon.
  void show_time(int hour, int minute, bool colon = true);
  void clear() { this->print(""); }
  void set_colon(bool on);
  /// 0 = brightest, 15 = off.
  void set_brightness(uint8_t level);
  uint8_t get_brightness() const { return this->brightness_; }

 protected:
  void set_segs_(const std::string &text);
  void send_digits_() { this->digits_dirty_ = true; }

  uint8_t segs_[4]{0, 0, 0, 0};
  uint8_t misc_{0};
  uint8_t brightness_{4};
  // Frames are queued and sent from loop(), at most one per FRAME_GAP_MS: the display MCU drops a frame that
  // follows another too closely. Only the latest digits / brightness are sent.
  bool digits_dirty_{false};
  bool brightness_dirty_{false};
  uint32_t last_frame_ms_{0};
};

}  // namespace esphome::hatch_display
