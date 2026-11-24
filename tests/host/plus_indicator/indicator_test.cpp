#include <cassert>
#include <limits>

#include "hatch_plus_indicator.h"
#include "esphome/components/hatch_plus_power/hatch_plus_power.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/hal.h"

using esphome::hatch_plus_indicator::HatchPlusIndicator;
using esphome::hatch_plus_power::HatchPlusPower;
using esphome::light::LightState;

static void tick(HatchPlusIndicator &output, uint32_t now) {
  esphome::test_millis = now;
  output.loop();
}

static void manual_colors() {
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

  const std::pair<esphome::light::LightColorValues, uint8_t> cases[] = {
      {{true, 0.01f, 0.02f, 0.1f, 0, 1}, 2},  // red: no dimming, ignore blue
      {{true, 1, 1, 0, 1, 0}, 1},              // green
      {{true, 1, 1, 1, 1, 0}, 3},              // yellow
      {{true, 1, 1, 0, 0, 1}, 0},              // blue only
      {{true, 1, 1, 1, 0, 0}, 2},
      {{true, 0, 1, 1, 1, 0}, 0},              // zero brightness
      {{true, 1, 1, 0, 1, 0}, 1},
      {{true, 1, 0, 1, 1, 0}, 0},              // zero color brightness
      {{true, 1, 1, 1, 1, 0}, 3},
      {{false, 1, 1, 1, 1, 0}, 0},             // explicit off
  };
  uint32_t now = 10;
  for (const auto &[values, color] : cases) {
    LightState light{values};
    const auto before = owner.attempts.size();
    output.write_state(&light);
    assert(owner.attempts.size() == before);  // the component loop owns hardware updates
    now += 500;
    tick(output, now);
    assert(owner.attempts.size() == before + 1);
    assert(owner.attempts.back() == std::make_pair(uint8_t{2}, color));
  }
}

static void readiness_retry_and_refresh() {
  HatchPlusPower owner;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  LightState light{{true, 1, 1, 1, 0, 0}};
  output.write_state(&light);
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
  light.current_values.green = 1;
  output.write_state(&light);
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
  manual_colors();
  readiness_retry_and_refresh();
}
