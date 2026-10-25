#include "hatch_display.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace esphome { uint32_t test_now = 0; }
using esphome::hatch_display::HatchDisplay;
void tick(HatchDisplay &display, uint32_t now) {
  esphome::test_now = now;
  display.loop();
}
void digits(const HatchDisplay &display, std::initializer_list<uint8_t> expected) {
  assert(!display.frames.empty());
  assert(display.frames.back().bytes == std::vector<uint8_t>(expected));
}
void start_and_repeat() {
  HatchDisplay d;
  esphome::test_now = 0;
  d.show_scrolling_text("123456");
  tick(d, 99); assert(d.frames.empty());
  tick(d, 100); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  tick(d, 2099); assert(d.frames.size() == 1);
  tick(d, 2100); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  tick(d, 2599); assert(d.frames.size() == 2);
  tick(d, 2600); digits(d, {8, 0x4f, 0x66, 0x6d, 0x7d, 0});
  tick(d, 4599); assert(d.frames.size() == 3);
  tick(d, 4600); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  tick(d, 6599); assert(d.frames.size() == 4);
  tick(d, 6600); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
}
void repeated_input_and_brightness() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_scrolling_text("12345"); tick(d, 100);
  d.show_scrolling_text("12345"); tick(d, 400);
  assert(d.frames.size() == 1);
  d.set_brightness(0); tick(d, 2050);
  digits(d, {0x18, 0xfe});
  tick(d, 2100); assert(d.frames.size() == 2);  // UART gap delays the next digit frame.
  tick(d, 2150); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  d.show_scrolling_text("12345");
  tick(d, 4149); assert(d.frames.size() == 3);
  tick(d, 4150); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  d.show_scrolling_text("67890"); tick(d, 4250);
  digits(d, {8, 0x7d, 0x07, 0x7f, 0x6f, 0});
}
void short_and_decimals() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_scrolling_text("1.23.4"); tick(d, 100);
  digits(d, {8, 0x86, 0x5b, 0xcf, 0x66, 0});
  tick(d, 10000); assert(d.frames.size() == 1);
  d.show_scrolling_text("1.23.4"); tick(d, 11000); assert(d.frames.size() == 1);
  d.show_scrolling_text("1.23.45"); tick(d, 11100);
  digits(d, {8, 0x86, 0x5b, 0xcf, 0x66, 0});
  tick(d, 13100); digits(d, {8, 0x5b, 0xcf, 0x66, 0x6d, 0});
  d.show_scrolling_text("7."); tick(d, 13200);
  digits(d, {8, 0, 0, 0, 0x87, 0});
  d.show_scrolling_text(""); tick(d, 13300);
  digits(d, {8, 0, 0, 0, 0, 0});
  tick(d, 20000); assert(d.frames.size() == 5);
}
void cancel() {
  for (int api = 0; api < 4; api++) {
    HatchDisplay d;
    esphome::test_now = 100;
    d.show_scrolling_text("12345"); tick(d, 100);
    tick(d, 2100);
    if (api == 0) d.print("12");
    if (api == 1) d.show_text("12");
    if (api == 2) d.show_time(12, 34);
    if (api == 3) d.clear();
    tick(d, 2200);
    auto last = d.frames.back().bytes;
    tick(d, 10000); assert(d.frames.size() == 3);
    assert(d.frames.back().bytes == last);
    d.show_scrolling_text("12345"); tick(d, 10100);
    digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  }
}
void no_catchup_and_wrap() {
  HatchDisplay d;
  uint32_t start = std::numeric_limits<uint32_t>::max() - 250;
  esphome::test_now = start;
  d.show_scrolling_text("123456"); tick(d, start);
  tick(d, start + 1999); assert(d.frames.size() == 1);
  tick(d, start + 2000); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  tick(d, start + 10000); digits(d, {8, 0x4f, 0x66, 0x6d, 0x7d, 0});
  tick(d, start + 10000); assert(d.frames.size() == 3);
  tick(d, start + 11999); assert(d.frames.size() == 3);
  tick(d, start + 12000); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
}
void startup_gap() {
  HatchDisplay d;
  esphome::test_now = 0;
  d.setup(); d.show_scrolling_text("12345");
  tick(d, 100); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  tick(d, 199); assert(d.frames.size() == 1);
  tick(d, 200); assert(d.frames.back().bytes.front() == 0x18);
  tick(d, 2100); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
}
void overlay_latest_content() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_time(12, 34); tick(d, 100);
  d.show_text_overlay("1234"); tick(d, 199); assert(d.frames.size() == 1);
  tick(d, 200); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  d.show_time(23, 45); tick(d, 1000); assert(d.frames.size() == 2);
  d.show_text("7"); tick(d, 2000); assert(d.frames.size() == 2);
  d.show_time(1, 23); tick(d, 3099); assert(d.frames.size() == 2);
  tick(d, 3100); digits(d, {8, 0, 0x06, 0x5b, 0x4f, 1});
  tick(d, 4000); assert(d.frames.size() == 3);
  // Static text and print requests are also retained, with their normal colon semantics.
  d.show_text_overlay("12", 500); tick(d, 4100);
  d.show_text("6"); tick(d, 4500);
  digits(d, {8, 0, 0, 0, 0x7d, 0});
  d.show_text_overlay("12", 500); tick(d, 4600);
  d.print("8"); tick(d, 5000);
  digits(d, {8, 0, 0, 0, 0x7f, 0});
}
void overlay_scroller_and_brightness() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_scrolling_text("123456"); tick(d, 100); tick(d, 2100);
  d.show_text_overlay("7."); tick(d, 2200);
  digits(d, {8, 0, 0, 0, 0x87, 0});
  d.set_brightness(15); tick(d, 2300); digits(d, {0x18, 0x0e});
  d.show_scrolling_text("123456"); tick(d, 2500); assert(d.frames.size() == 4);
  tick(d, 5099); assert(d.frames.size() == 4);
  tick(d, 5100); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  tick(d, 7099); assert(d.frames.size() == 5);
  tick(d, 7100); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  d.show_text_overlay("1", 500); tick(d, 7200);
  d.show_scrolling_text("67890"); tick(d, 7300); assert(d.frames.size() == 7);
  tick(d, 7600); digits(d, {8, 0x7d, 0x07, 0x7f, 0x6f, 0});
}
void overlay_replacement_wrap_and_gap() {
  HatchDisplay d;
  uint32_t start = std::numeric_limits<uint32_t>::max() - 250;
  esphome::test_now = start;
  d.show_text("1");
  d.show_text_overlay("2", 500); tick(d, start);
  d.show_text_overlay("3", 800); tick(d, start + 99); assert(d.frames.size() == 1);
  tick(d, start + 100); digits(d, {8, 0, 0, 0, 0x4f, 0});
  d.set_brightness(0); tick(d, start + 750);
  tick(d, start + 800); assert(d.frames.size() == 3); // Restore waits for UART gap.
  tick(d, start + 850); digits(d, {8, 0, 0, 0, 0x06, 0});
  assert(d.frames.size() == 4);
}
void once_scroll_and_latest_clock() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_time(12, 34); tick(d, 100);
  d.show_scrolling_text_once("1.23.456");
  tick(d, 199); assert(d.frames.size() == 1);
  tick(d, 200); digits(d, {8, 0x86, 0x5b, 0xcf, 0x66, 0});
  d.show_time(23, 45); tick(d, 1000); assert(d.frames.size() == 2);
  tick(d, 2199); assert(d.frames.size() == 2);
  tick(d, 2200); digits(d, {8, 0x5b, 0xcf, 0x66, 0x6d, 0});
  tick(d, 2699); assert(d.frames.size() == 3);
  tick(d, 2700); digits(d, {8, 0xcf, 0x66, 0x6d, 0x7d, 0});
  d.show_time(1, 23); tick(d, 4699); assert(d.frames.size() == 4);
  tick(d, 4700); digits(d, {8, 0, 0x06, 0x5b, 0x4f, 1});
  tick(d, 10000); assert(d.frames.size() == 5);
}
void once_short_and_underlying_updates() {
  for (const char *text : {"", "7.", "1.23.4"}) {
    HatchDisplay d;
    esphome::test_now = 0;
    d.show_time(12, 34);
    d.show_scrolling_text_once(text);
    tick(d, 99); assert(d.frames.empty());
    tick(d, 100); assert(d.frames.size() == 1);
    if (std::string(text).empty()) digits(d, {8, 0, 0, 0, 0, 0});
    else if (std::string(text) == "7.") digits(d, {8, 0, 0, 0, 0x87, 0});
    else digits(d, {8, 0x86, 0x5b, 0xcf, 0x66, 0});
    d.show_text("8"); tick(d, 200);
    d.print("6"); tick(d, 2099); assert(d.frames.size() == 1);
    tick(d, 2100); digits(d, {8, 0, 0, 0, 0x7d, 0});
    tick(d, 10000); assert(d.frames.size() == 2);
  }
}
void once_replacements() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_text("8");
  d.show_scrolling_text_once("123456"); tick(d, 100);
  tick(d, 2100); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  // Even identical input is a new one-shot, restarting the first frame.
  d.show_scrolling_text_once("123456"); tick(d, 2200);
  digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  d.show_scrolling_text_once("7."); tick(d, 2300);
  digits(d, {8, 0, 0, 0, 0x87, 0});
  tick(d, 4299); assert(d.frames.size() == 4);
  tick(d, 4300); digits(d, {8, 0, 0, 0, 0x7f, 0});
  // A volume overlay replaces a one-shot and vice versa, with no deferred overlay queue.
  d.show_scrolling_text_once("123456"); tick(d, 4400);
  d.show_text_overlay("2", 500); tick(d, 4500);
  digits(d, {8, 0, 0, 0, 0x5b, 0});
  tick(d, 4900); digits(d, {8, 0, 0, 0, 0x7f, 0});
  d.show_text_overlay("3", 5000); tick(d, 5000);
  d.show_scrolling_text_once("4"); tick(d, 5100);
  digits(d, {8, 0, 0, 0, 0x66, 0});
  tick(d, 7100); digits(d, {8, 0, 0, 0, 0x7f, 0});
  tick(d, 15000); assert(d.frames.size() == 11);
}
void once_underlying_scroller_restarts() {
  HatchDisplay d;
  esphome::test_now = 100;
  d.show_scrolling_text("123456"); tick(d, 100); tick(d, 2100);
  d.show_scrolling_text_once("7"); tick(d, 2200);
  d.show_scrolling_text("123456"); tick(d, 3000);
  tick(d, 4200); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  tick(d, 6199); assert(d.frames.size() == 4);
  tick(d, 6200); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  d.show_scrolling_text_once("8"); tick(d, 6300);
  d.show_scrolling_text("67890"); tick(d, 7000);
  tick(d, 8300); digits(d, {8, 0x7d, 0x07, 0x7f, 0x6f, 0});
  tick(d, 10299); assert(d.frames.size() == 7);
  tick(d, 10300); digits(d, {8, 0x07, 0x7f, 0x6f, 0x3f, 0});
}
void once_gaps_no_catchup_and_wrap() {
  HatchDisplay d;
  uint32_t start = std::numeric_limits<uint32_t>::max() - 250;
  esphome::test_now = start;
  d.show_time(12, 34);
  d.set_brightness(0); tick(d, start); tick(d, start + 100);
  d.show_scrolling_text_once("123456");
  tick(d, start + 199); assert(d.frames.size() == 2);
  tick(d, start + 200); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 0});
  d.set_brightness(15); tick(d, start + 2150); digits(d, {0x18, 0x0e});
  tick(d, start + 2200); assert(d.frames.size() == 4);
  tick(d, start + 2250); digits(d, {8, 0x5b, 0x4f, 0x66, 0x6d, 0});
  tick(d, start + 2749); assert(d.frames.size() == 5);
  tick(d, start + 10000); digits(d, {8, 0x4f, 0x66, 0x6d, 0x7d, 0});
  tick(d, start + 10000); assert(d.frames.size() == 6);
  d.set_brightness(0); tick(d, start + 11950);
  tick(d, start + 12000); assert(d.frames.size() == 7);
  tick(d, start + 12050); digits(d, {8, 0x06, 0x5b, 0x4f, 0x66, 1});
  assert(d.frames.size() == 8);
  for (size_t i = 1; i < d.frames.size(); i++)
    assert(uint32_t(d.frames[i].at - d.frames[i - 1].at) >= 100);
}
int main() {
  start_and_repeat(); repeated_input_and_brightness(); short_and_decimals();
  cancel(); no_catchup_and_wrap(); startup_gap();
  overlay_latest_content(); overlay_scroller_and_brightness(); overlay_replacement_wrap_and_gap();
  once_scroll_and_latest_clock(); once_short_and_underlying_updates(); once_replacements();
  once_underlying_scroller_restarts(); once_gaps_no_catchup_and_wrap();
}
