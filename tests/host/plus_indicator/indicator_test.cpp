#include <cassert>
#include <limits>

#include "hatch_plus_indicator.h"
#include "esphome/components/hatch_plus_power/hatch_plus_power.h"
#include "esphome/core/hal.h"

using esphome::hatch_plus_indicator::Color;
using esphome::hatch_plus_indicator::HatchPlusIndicator;
using esphome::hatch_plus_power::HatchPlusPower;

static void tick(HatchPlusIndicator &output, uint32_t now) {
  esphome::test_millis = now;
  output.loop();
}

static void colors() {
  HatchPlusPower owner;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(2);
  tick(output, 0);
  assert(owner.attempts.empty());
  owner.ready = true;
  tick(output, 10);
  assert(owner.attempts.size() == 1);
  assert(owner.attempts.back() == std::make_pair(uint8_t{2}, uint8_t{0}));  // off at boot

  // Power states: 0 off, 1 green, 2 red, 3 yellow.
  const std::pair<Color, uint8_t> cases[] = {
      {Color::RED, 2}, {Color::GREEN, 1}, {Color::YELLOW, 3}, {Color::OFF, 0}, {Color::RED, 2},
  };
  uint32_t now = 10;
  for (const auto &[color, state] : cases) {
    const auto before = owner.attempts.size();
    output.set_color(color);
    assert(owner.attempts.size() == before);  // the component loop owns hardware updates
    now += 500;
    tick(output, now);
    assert(owner.attempts.size() == before + 1);
    assert(owner.attempts.back() == std::make_pair(uint8_t{2}, state));
  }
}

static void readiness_retry_and_refresh() {
  HatchPlusPower owner;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  output.set_color(Color::RED);
  tick(output, 0);
  assert(owner.attempts.empty());

  // Fail the first write just before the millisecond clock wraps.
  const auto start = std::numeric_limits<uint32_t>::max() - 250;
  owner.ready = true;
  owner.succeed = false;
  tick(output, start);
  assert(owner.attempts.size() == 1);
  assert(owner.attempts.back() == std::make_pair(uint8_t{1}, uint8_t{2}));
  tick(output, start + 499);
  assert(owner.attempts.size() == 1);

  // Retry the latest requested state, not the failed snapshot.
  output.set_color(Color::YELLOW);
  owner.succeed = true;
  tick(output, start + 500);
  assert(owner.attempts.size() == 2);
  const auto yellow = std::make_pair(uint8_t{1}, uint8_t{3});
  assert(owner.attempts.back() == yellow);
  tick(output, start + 5499);
  assert(owner.attempts.size() == 2);
  tick(output, start + 5500);
  assert(owner.attempts.size() == 3);
  assert(owner.attempts.back() == yellow);

  // Suspension invalidates the cached state; recovery reasserts it without
  // waiting for the next periodic refresh.
  owner.ready = false;
  tick(output, start + 6000);
  assert(owner.attempts.size() == 3);
  owner.ready = true;
  tick(output, start + 6001);
  assert(owner.attempts.size() == 4);
  assert(owner.attempts.back() == yellow);
}

int main() {
  colors();
  readiness_retry_and_refresh();
}
