#include "hatch_display.h"
#include "esphome/core/hal.h"
#include <cassert>
#include <iostream>
using esphome::clock_ms;
esphome::hatch_display::HatchDisplay clock_display;
struct { bool state{true}; } show_clock;
struct { std::string state; } display_text;
struct { float state{11}; } display_brightness;
struct Time {
  int hour{0}, minute{0}; bool valid{false};
  bool is_valid() const { return valid; }
};
struct { Time value; Time now() const { return value; } } ha_time;
#define id(name) name
#include "config_lambdas.h"
#undef id
using Frame = std::vector<uint8_t>;
void tick() { clock_ms += 100; clock_display.loop(); }
void digits(Frame expected) {
  size_t before = clock_display.frames.size(); tick();
  assert(clock_display.frames.size() == before + 1);
  assert(clock_display.frames.back() == expected);
}
void quiet() {
  size_t before = clock_display.frames.size(); tick();
  assert(clock_display.frames.size() == before);
}
int main() {
  clock_display.setup();
  // Boot blanks before enabling light, and enforces the actual 100 ms frame gap.
  clock_display.loop(); assert(clock_display.frames.empty());
  digits({0x08,0,0,0,0,0});
  clock_ms += 99; clock_display.loop(); assert(clock_display.frames.size() == 1);
  ++clock_ms; clock_display.loop();
  assert(clock_display.frames.back() == Frame({0x18,0xde})); quiet();
  // No valid HA time leaves the boot display blank; there is no fallback clock.
  update_display(); quiet();
  ha_time.value = {0,0,true}; update_display();
  digits({0x08,0x06,0x5b,0x3f,0x3f,1}); // Midnight -> 12:00.
  ha_time.value = {12,0,true}; update_display();
  digits({0x08,0x06,0x5b,0x3f,0x3f,1}); // Noon -> 12:00.
  ha_time.value = {13,5,true}; update_display();
  digits({0x08,0,0x06,0x3f,0x6d,1}); // 1:05, no leading hour zero.
  ha_time.value.minute = 6; update_display();
  digits({0x08,0,0x06,0x3f,0x7d,1});
  ha_time.value = {23,59,true}; update_display();
  digits({0x08,0x06,0x06,0x6d,0x6f,1});
  // Invalid time after a valid clock deliberately retains the last digits.
  ha_time.value.valid = false; update_display(); quiet();
  show_clock.state = false; display_text.state = "1234"; update_display();
  digits({0x08,0x06,0x5b,0x4f,0x66,0}); // Text works even without HA time.
  display_text.state = "12.3"; update_display();
  digits({0x08,0,0x06,0xdb,0x4f,0}); // Decimal point and right alignment.
  display_text.state = "12345678"; update_display();
  if constexpr (text_scrolls) {
    digits({0x08,0x06,0x5b,0x4f,0x66,0}); // First four glyphs, then a 2s initial pause.
    const uint32_t first_frame = clock_ms;
    update_display(); quiet(); // Repeated update does not restart the scroller.
    clock_ms = first_frame + 1999; clock_display.loop();
    assert(clock_display.frames.back() == Frame({0x08,0x06,0x5b,0x4f,0x66,0}));
    ++clock_ms; clock_display.loop();
    assert(clock_display.frames.back() == Frame({0x08,0x5b,0x4f,0x66,0x6d,0}));
    clock_ms += 400; digits({0x08,0x4f,0x66,0x6d,0x7d,0});
  } else {
    digits({0x08,0x6d,0x7d,0x07,0x7f,0}); // Static mode keeps its last-four behavior.
  }
  display_text.state.clear(); update_display(); digits({0x08,0,0,0,0,0});
  show_clock.state = true; update_display(); quiet();
  ha_time.value = {7,9,true}; update_display();
  digits({0x08,0,0x07,0x3f,0x6f,1});
  clock_display.show_scrolling_text_once("12345");
  digits({0x08,0x06,0x5b,0x4f,0x66,0});
  ha_time.value = {8,10,true}; update_display(); quiet();
  clock_ms += 1800; digits({0x08,0x5b,0x4f,0x66,0x6d,0});
  ha_time.value.minute = 11; update_display(); quiet();
  clock_ms += 1800; digits({0x08,0,0x7f,0x06,0x06,1});
  quiet();
  // Execute the generated number action for the entire allowed entity range.
  const uint8_t encoded[16] = {0xfe,0x7e,0xbe,0x3e,0xde,0x5e,0x9e,0x1e,
                               0xee,0x6e,0xae,0x2e,0xce,0x4e,0x8e,0x0e};
  for (int ui=0; ui<=15; ++ui) {
    set_display_brightness(ui);
    assert(clock_display.get_brightness() == 15-ui);
    digits({0x18,encoded[15-ui]});
  }
  display_brightness.state = 11; boot_brightness();
  assert(clock_display.get_brightness() == 4); digits({0x18,0xde});
  clock_display.set_brightness(255); assert(clock_display.get_brightness() == 15);
  digits({0x18,0x0e});
  // Latest-wins queue: one digits frame then one brightness frame, not a burst.
  clock_display.show_time(8,10); clock_display.show_time(9,11);
  set_display_brightness(1); set_display_brightness(15);
  digits({0x08,0,0x6f,0x06,0x06,1}); digits({0x18,0xfe}); quiet();
  assert(clock_display.flushes == clock_display.frames.size());
  std::cout << "clock: generated lambdas + real UART frames passed\n";
}
