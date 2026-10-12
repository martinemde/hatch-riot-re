#include "hatch_display.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome::hatch_display {

static const char *const TAG = "hatch_display";

static const uint8_t FRAME_DIGITS = 0x08, FRAME_BRIGHTNESS = 0x18;
static const uint8_t MISC_COLON = 0x01;
static const uint8_t SEG_DP = 0x80;

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

void HatchDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch clock display:\n  Brightness level: %u (0 = brightest, 15 = off)", this->brightness_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_ODD, 8);
}

void HatchDisplay::print(const std::string &text) {
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
  memcpy(this->segs_, segs, sizeof(segs));
  this->send_digits_();
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
  uint8_t f[2] = {FRAME_BRIGHTNESS, BRIGHTNESS[this->brightness_]};
  this->write_array(f, sizeof(f));
}

void HatchDisplay::send_digits_() {
  uint8_t f[6] = {FRAME_DIGITS, this->segs_[0], this->segs_[1], this->segs_[2], this->segs_[3],
                  uint8_t(this->misc_ & 0x07)};
  this->write_array(f, sizeof(f));
}

}  // namespace esphome::hatch_display
