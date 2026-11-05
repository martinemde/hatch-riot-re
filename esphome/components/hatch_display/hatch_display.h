#pragma once

#include <string>
#include <vector>

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
  /// Show static text over the latest base content. A zero duration holds it until replaced or cleared.
  void show_text_overlay(const std::string &text, uint32_t duration_ms = 3000);
  void clear_overlay() { if (this->overlay_active_) this->finish_overlay_(); }
  /// Temporarily scroll text once over the underlying display, then restore its latest content.
  /// Hold the first and last frames for 2000 ms each, with 500 ms steps; <=4 glyphs show for 2000 ms total.
  /// A new overlay replaces any previous overlay. Timings start when each frame is sent.
  void show_scrolling_text_once(const std::string &text);
  /// Slide text longer than four glyphs every 500 ms; hold the first and final frames for 2000 ms, then repeat.
  /// Decimal points attach to the preceding glyph. Repeating the same input preserves its position.
  void show_scrolling_text(const std::string &text);
  /// Show HH:MM (or H:MM) with the colon.
  void show_time(int hour, int minute, bool colon = true);
  void clear() { this->print(""); }
  void set_colon(bool on);
  /// 0 = brightest, 15 = off.
  void set_brightness(uint8_t level);
  uint8_t get_brightness() const { return this->brightness_; }

 protected:
  void set_segs_(const std::string &text);
  static void encode_text_(const std::string &text, uint8_t *output);
  struct ScrollState {
    std::vector<uint8_t> glyphs;
    size_t offset{0};
    uint32_t frame_ms{0};
    bool frame_pending{false};
  };
  static void init_scroll_(const std::string &text, ScrollState &scroll, uint8_t *output);
  static void set_scroll_frame_(ScrollState &scroll, uint8_t *output);
  /// Return false when a one-shot has finished; advance at most one frame per call.
  static bool advance_scroll_(ScrollState &scroll, uint8_t *output, uint32_t now, bool repeat);
  void finish_overlay_();
  void send_digits_() { this->digits_dirty_ = true; }

  uint8_t segs_[4]{0, 0, 0, 0};
  uint8_t misc_{0};
  uint8_t brightness_{4};
  // Frames are queued and sent from loop(), at most one per FRAME_GAP_MS: the display MCU drops a frame that
  // follows another too closely. Only the latest digits / brightness are sent.
  bool digits_dirty_{false};
  bool brightness_dirty_{false};
  uint32_t last_frame_ms_{0};
  bool scrolling_text_{false};
  std::string scroll_text_;
  ScrollState scroll_;
  ScrollState overlay_scroll_;
  bool overlay_scrolling_{false};
  bool overlay_active_{false};
  bool overlay_dirty_{false};
  uint8_t overlay_segs_[4]{0, 0, 0, 0};
  uint32_t overlay_started_ms_{0};
  uint32_t overlay_duration_ms_{0};
};

}  // namespace esphome::hatch_display
