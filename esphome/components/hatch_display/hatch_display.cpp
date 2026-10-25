#include "hatch_display.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::hatch_display {

static const char *const TAG = "hatch_display";

static const uint8_t FRAME_DIGITS = 0x08, FRAME_BRIGHTNESS = 0x18;
static const uint8_t MISC_COLON = 0x01;
static const uint8_t SEG_DP = 0x80;
// Stock waits for TX done plus 10 ticks after every frame; 100 ms covers a 100 Hz tick.
static const uint32_t FRAME_GAP_MS = 100;
static const uint32_t SCROLL_STEP_MS = 500;
static const uint32_t SCROLL_START_PAUSE_MS = 2000;
static const uint32_t SCROLL_END_PAUSE_MS = 2000;

// Stock font, ASCII 0x20..0x7E (firmware table at 0x3f486910).
static const uint8_t FONT[95] = {
    0x00, 0x86, 0x22, 0x7e, 0x6d, 0xd2, 0x46, 0x20, 0x29, 0x0b, 0x21, 0x70, 0x10, 0x40, 0x80, 0x52,  // ' '../
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x09, 0x0d, 0x61, 0x48, 0x43, 0xd3,  // 0..?
    0x5f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, 0x3d, 0x76, 0x30, 0x1e, 0x75, 0x38, 0x15, 0x37, 0x3f,  // @..O
    0x73, 0x6b, 0x33, 0x6d, 0x78, 0x3e, 0x3e, 0x2a, 0x76, 0x6e, 0x5b, 0x39, 0x64, 0x0f, 0x23, 0x08,  // P.._
    0x02, 0x5f, 0x7c, 0x58, 0x5e, 0x7b, 0x71, 0x6f, 0x74, 0x10, 0x0c, 0x75, 0x30, 0x14, 0x54, 0x5c,  // `..o
    0x73, 0x67, 0x50, 0x6d, 0x78, 0x1c, 0x1c, 0x14, 0x76, 0x6e, 0x5b, 0x46, 0x30, 0x70, 0x01,        // p..~
};
// Brightness bytes for level 0..15 (stock table at 0x3f432e0c). Level 0 is brightest, 15 is off.
static const uint8_t BRIGHTNESS[16] = {0xfe, 0x7e, 0xbe, 0x3e, 0xde, 0x5e, 0x9e, 0x1e,
                                       0xee, 0x6e, 0xae, 0x2e, 0xce, 0x4e, 0x8e, 0x0e};

static uint8_t encode(char c) { return (c >= 0x20 && c <= 0x7e) ? FONT[c - 0x20] : 0x00; }

void HatchDisplay::setup() {
  // The display stays dark until it gets a brightness frame. Blank it first, as stock does.
  this->send_digits_();
  this->set_brightness(this->brightness_);
}

void HatchDisplay::loop() {
  uint32_t now = millis();
  if (this->overlay_active_) {
    if (this->overlay_scrolling_) {
      if (!this->advance_scroll_(this->overlay_scroll_, this->overlay_segs_, now, false))
        this->finish_overlay_();
      else if (this->overlay_scroll_.frame_pending)
        this->overlay_dirty_ = true;
    } else if (now - this->overlay_started_ms_ >= this->overlay_duration_ms_) {
      this->finish_overlay_();
    }
  }
  if (!this->overlay_active_ && this->scrolling_text_) {
    this->advance_scroll_(this->scroll_, this->segs_, now, true);
    if (this->scroll_.frame_pending)
      this->send_digits_();
  }
  bool send_digits = this->overlay_active_ ? this->overlay_dirty_ : this->digits_dirty_;
  if (!send_digits && !this->brightness_dirty_)
    return;
  if (now - this->last_frame_ms_ < FRAME_GAP_MS)
    return;
  // Digits first, so the display is blanked before its first brightness frame lights it at boot.
  if (send_digits) {
    const uint8_t *segs = this->overlay_active_ ? this->overlay_segs_ : this->segs_;
    uint8_t f[6] = {FRAME_DIGITS, segs[0], segs[1], segs[2], segs[3],
                    uint8_t(this->overlay_active_ ? 0 : (this->misc_ & 0x07))};
    this->write_array(f, sizeof(f));
    if (this->overlay_active_)
      this->overlay_dirty_ = false;
    else
      this->digits_dirty_ = false;
    ScrollState &scroll = this->overlay_active_ ? this->overlay_scroll_ : this->scroll_;
    if (scroll.frame_pending && (this->overlay_active_ ? this->overlay_scrolling_ : this->scrolling_text_)) {
      scroll.frame_ms = millis();
      scroll.frame_pending = false;
    }
  } else {
    this->brightness_dirty_ = false;
    uint8_t f[2] = {FRAME_BRIGHTNESS, BRIGHTNESS[this->brightness_]};
    this->write_array(f, sizeof(f));
  }
  this->flush();
  this->last_frame_ms_ = millis();
}

void HatchDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch clock display:\n  Brightness level: %u (0 = brightest, 15 = off)", this->brightness_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_ODD, 8);
}

void HatchDisplay::print(const std::string &text) {
  this->scrolling_text_ = false;
  this->scroll_.frame_pending = false;
  this->set_segs_(text);
  this->send_digits_();
}

void HatchDisplay::show_text(const std::string &text) {
  this->misc_ &= ~MISC_COLON;
  this->print(text);
}

void HatchDisplay::show_scrolling_text(const std::string &text) {
  if (this->scrolling_text_ && this->scroll_text_ == text)
    return;
  this->scrolling_text_ = true;
  this->scroll_text_ = text;
  this->misc_ &= ~MISC_COLON;
  this->init_scroll_(text, this->scroll_, this->segs_);
  this->send_digits_();
}

void HatchDisplay::init_scroll_(const std::string &text, ScrollState &scroll, uint8_t *output) {
  scroll.glyphs.clear();
  for (char c : text) {
    if (c == '.' && !scroll.glyphs.empty())
      scroll.glyphs.back() |= SEG_DP;
    else
      scroll.glyphs.push_back(encode(c));
  }
  scroll.offset = 0;
  set_scroll_frame_(scroll, output);
}

void HatchDisplay::set_scroll_frame_(ScrollState &scroll, uint8_t *output) {
  memset(output, 0, 4);
  size_t count = scroll.glyphs.size() < 4 ? scroll.glyphs.size() : 4;
  for (size_t i = 0; i < count; i++)
    output[4 - count + i] = scroll.glyphs[scroll.offset + i];
  scroll.frame_pending = true;
}

bool HatchDisplay::advance_scroll_(ScrollState &scroll, uint8_t *output, uint32_t now, bool repeat) {
  if (scroll.frame_pending || (repeat && scroll.glyphs.size() <= 4))
    return true;
  bool at_end = scroll.glyphs.size() <= 4 || scroll.offset == scroll.glyphs.size() - 4;
  uint32_t interval = at_end ? SCROLL_END_PAUSE_MS
                             : (scroll.offset == 0 ? SCROLL_START_PAUSE_MS : SCROLL_STEP_MS);
  if (now - scroll.frame_ms < interval)
    return true;
  if (at_end && !repeat)
    return false;
  scroll.offset = at_end ? 0 : scroll.offset + 1;
  set_scroll_frame_(scroll, output);
  return true;
}

void HatchDisplay::show_scrolling_text_once(const std::string &text) {
  this->init_scroll_(text, this->overlay_scroll_, this->overlay_segs_);
  this->overlay_active_ = true;
  this->overlay_scrolling_ = true;
  this->overlay_dirty_ = true;
}

void HatchDisplay::finish_overlay_() {
  this->overlay_active_ = false;
  this->overlay_scrolling_ = false;
  this->overlay_dirty_ = false;
  if (this->scrolling_text_) {
    this->scroll_.offset = 0;
    this->set_scroll_frame_(this->scroll_, this->segs_);
  }
  this->send_digits_();
}

void HatchDisplay::show_text_overlay(const std::string &text, uint32_t duration_ms) {
  this->encode_text_(text, this->overlay_segs_);
  this->overlay_scrolling_ = false;
  this->overlay_started_ms_ = millis();
  this->overlay_duration_ms_ = duration_ms;
  this->overlay_active_ = true;
  this->overlay_dirty_ = true;
}

void HatchDisplay::set_segs_(const std::string &text) {
  this->encode_text_(text, this->segs_);
}

void HatchDisplay::encode_text_(const std::string &text, uint8_t *output) {
  uint8_t segs[4] = {0, 0, 0, 0};
  // Walk backwards so the text is right-aligned and a '.' attaches to the character before it.
  int digit = 3;
  for (int i = int(text.size()) - 1; i >= 0 && digit >= 0; i--) {
    if (text[i] == '.' && i > 0) {
      segs[digit] = encode(text[i - 1]) | SEG_DP;
      i--;
    } else {
      segs[digit] = encode(text[i]);
    }
    digit--;
  }
  memcpy(output, segs, sizeof(segs));
}

void HatchDisplay::show_time(int hour, int minute, bool colon) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%2d%02d", hour % 100, minute % 100);
  this->misc_ = colon ? (this->misc_ | MISC_COLON) : (this->misc_ & ~MISC_COLON);
  this->print(buf);
}

void HatchDisplay::set_colon(bool on) {
  this->misc_ = on ? (this->misc_ | MISC_COLON) : (this->misc_ & ~MISC_COLON);
  this->send_digits_();
}

void HatchDisplay::set_brightness(uint8_t level) {
  this->brightness_ = level > 15 ? 15 : level;
  this->brightness_dirty_ = true;
}

}  // namespace esphome::hatch_display
