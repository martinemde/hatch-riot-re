#include "hatch_plus_indicator.h"
#include "esphome/core/hal.h"
#include "esphome/components/wifi/wifi_component.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

using namespace esphome;
using hatch_plus_indicator::HatchPlusIndicator;
using hatch_plus_indicator::AutomaticEffect;
using hatch_plus_power::HatchPlusPower;

static void battery() {
  HatchPlusPower owner;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  const std::pair<float, uint8_t> cases[] = {
    {0, 2}, {24.99f, 2}, {25, 3}, {49.99f, 3}, {50, 1}, {100, 1},
    {-0.01f, 0}, {100.01f, 0}, {NAN, 0}, {INFINITY, 0}, {-INFINITY, 0},
  };
  for (const auto &[percent, expected] : cases) {
    owner.battery = percent;
    assert(output.automatic_color() == expected);
  }
}

static void wifi_and_lock() {
  wifi::WiFiComponent network;
  wifi::global_wifi_component = &network;
  HatchPlusIndicator output;
  output.set_indicator(0);
  assert(output.automatic_color() == 2);
  network.connected = true;
  assert(output.automatic_color() == 1);
  wifi::global_wifi_component = nullptr;
  assert(output.automatic_color() == 2);

  hatch_plus::HatchPlus controller;
  output.set_hatch_plus(&controller);
  output.set_indicator(2);
  assert(output.automatic_color() == 0);
  controller.locked = true;
  assert(output.automatic_color() == 2);
  controller.locked = false;
  assert(output.automatic_color() == 0);
}

static void tick(HatchPlusIndicator &output, uint32_t now) {
  test_millis = now;
  output.loop();
}

static void manual_and_automatic() {
  HatchPlusPower owner;
  owner.battery = 60;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  light::LightState state;
  AutomaticEffect effect(&output);
  effect.init_internal(&state);
  test_millis = 0;
  effect.start();
  effect.apply();
  assert(state.calls == 0);
  output.write_state(&state);
  tick(output, 0);
  assert(owner.writes.back() == std::make_pair(uint8_t(1), uint8_t(1)));

  // ESPHome stops Automatic when a request selects effect None. Charging must
  // not blink a manually selected color, including at an odd 500ms phase.
  owner.charging = true;
  state.make_call().set_effect("None").perform();
  effect.stop();
  assert(state.effect == "None");
  const std::pair<light::LightColorValues, uint8_t> cases[] = {
    {{1, 1, 1, 1, 0, 0}, 2},  // red
    {{1, 1, 1, 0, 1, 0}, 1},  // green
    {{1, 1, 1, 1, 1, 0}, 3},  // yellow
    {{1, 1, 1, 0, 0, 1}, 0},  // blue has no hardware channel
    {{1, 0.01f, 1, 1, 0, 0}, 2},  // any positive brightness
    {{1, 0, 1, 1, 1, 0}, 0},
    {{1, 1, 0, 1, 1, 0}, 0},
    {{0, 1, 1, 1, 1, 0}, 0},  // explicit off
  };
  uint32_t now = 500;
  for (const auto &[values, color] : cases) {
    state.current_values = values;
    state.remote_values = values;
    output.write_state(&state);
    tick(output, now);
    assert(owner.writes.back().second == color);
    now += 5000;
    tick(output, now);
    assert(owner.writes.back().second == color);
    now += 500;
  }
  // Selecting Automatic again uses the physical status, not the manual RGB.
  owner.charging = false;
  state.make_call().set_state(true).set_effect("Automatic").perform();
  test_millis = now;
  effect.start();
  effect.apply();
  output.write_state(&state);
  output.loop();
  assert(owner.writes.back().second == 1);
  assert(state.current_values.is_on());
}

static void assert_logical(const light::LightState &state) {
  // Automatic status is entirely local. HA retains the user's show/hide state
  // and manual color/brightness, even as physical status colors change.
  for (const auto *values : {&state.current_values, &state.remote_values}) {
    assert(values->is_on());
    assert(values->get_brightness() == 1);
    assert(values->get_color_brightness() == 1);
    assert(values->get_red() == 0);
    assert(values->get_green() == 0);
    assert(values->get_blue() == 0);
  }
}

static void blinking() {
  HatchPlusPower owner;
  owner.battery = 25;
  owner.charging = true;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  light::LightState state;
  AutomaticEffect effect(&output);
  effect.init_internal(&state);
  test_millis = 0;
  effect.start();
  for (uint32_t now : {0u, 499u, 500u, 999u, 1000u, 1499u, 1500u, 1999u}) {
    test_millis = now;
    const uint8_t expected = (now / 500) % 2 ? 0 : 3;
    assert(output.automatic_color() == 3);
    effect.apply();
    output.write_state(&state);
    output.loop();
    assert(owner.writes.back().second == expected);
    assert_logical(state);
    assert(state.calls == 0);
    assert(state.publications == 0);
  }
  assert(owner.writes.size() == 4);
  // Cross a threshold during a dark phase without any HA publication.
  // The physical LED must stay dark until the next 500ms boundary.
  test_millis = 2000;
  effect.apply();
  output.write_state(&state);
  output.loop();
  owner.battery = 50;
  test_millis = 2500;
  effect.apply();
  output.write_state(&state);
  output.loop();
  assert(owner.writes.back().second == 0);
  assert_logical(state);
  assert(state.publications == 0);
  test_millis = 3000;
  effect.apply();
  output.write_state(&state);
  output.loop();
  assert(owner.writes.back().second == 1);
  assert_logical(state);
  assert(state.publications == 0);

  // Stopping charging on what would be a dark phase leaves a steady LED,
  // without changing or republishing the already-correct HA state.
  owner.charging = false;
  for (uint32_t now : {3500u, 4000u, 4500u}) {
    test_millis = now;
    effect.apply();
    output.write_state(&state);
    output.loop();
    assert(owner.writes.back().second == 1);
    assert_logical(state);
    assert(state.publications == 0);
  }
}

static void automatic_explicit_off() {
  HatchPlusPower owner;
  owner.battery = 60;
  owner.charging = true;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(1);
  light::LightState state;
  output.setup_state(&state);
  state.effect = "Automatic";
  AutomaticEffect effect(&output);
  effect.init_internal(&state);
  test_millis = 0;
  effect.start();
  effect.apply();
  output.write_state(&state);
  output.loop();
  assert(owner.writes.back().second == 1);

  // Model native LightState resume_effect: off saves the selected effect,
  // stops it, and on restores that saved effect without extra publications.
  state.make_call().set_state(false).set_effect("None").perform();
  effect.stop();
  output.write_state(&state);
  const auto publications = state.publications;
  for (uint32_t now : {500u, 1000u, 1500u, 2000u}) {
    tick(output, now);
    assert(owner.writes.back().second == 0);
    assert(!state.remote_values.is_on());
    assert(state.publications == publications);
  }
  state.make_call().set_state(true).set_effect("Automatic").perform();
  effect.start();
  effect.apply();
  output.write_state(&state);
  for (uint32_t now : {2500u, 3000u, 3500u}) {
    tick(output, now);
    assert(owner.writes.back().second == ((now / 500) % 2 ? 0 : 1));
    assert(state.remote_values.is_on());
    assert(state.publications == publications + 1);
  }
}

static void local_status() {
  HatchPlusPower owner;
  hatch_plus::HatchPlus controller;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_hatch_plus(&controller);
  output.set_indicator(2);
  light::LightState state;
  state.current_values = {1, 0.3f, 0.4f, 0.2f, 0.6f, 0.8f};
  state.remote_values = state.current_values;
  AutomaticEffect effect(&output);
  effect.init_internal(&state);
  test_millis = 0;
  effect.start();
  for (uint32_t now : {0u, 500u, 1000u}) {
    controller.locked = now == 500;
    test_millis = now;
    effect.apply();
    output.write_state(&state);
    output.loop();
    assert(owner.writes.back().second == (controller.locked ? 2 : 0));
    for (const auto *values : {&state.current_values, &state.remote_values}) {
      assert(values->is_on());
      assert(values->get_brightness() == 0.3f);
      assert(values->get_color_brightness() == 0.4f);
      assert(values->get_red() == 0.2f);
      assert(values->get_green() == 0.6f);
      assert(values->get_blue() == 0.8f);
    }
    assert(state.publications == 0);
  }
}

static void retry() {
  HatchPlusPower owner;
  HatchPlusIndicator output;
  output.set_power(&owner);
  output.set_indicator(2);
  light::LightState state;
  state.current_values.red = 1;
  output.write_state(&state);
  owner.ready = false;
  tick(output, 0);
  assert(owner.attempts.empty());
  owner.ready = true;
  owner.succeed = false;
  tick(output, 0);
  assert(owner.attempts.size() == 1 && owner.writes.empty());
  tick(output, 499);
  assert(owner.attempts.size() == 1);
  owner.succeed = true;
  tick(output, 500);
  assert(owner.attempts.size() == 2 && owner.writes.size() == 1);
  assert(owner.writes.back() == std::make_pair(uint8_t(2), uint8_t(2)));
  tick(output, 5499);
  assert(owner.attempts.size() == 2);
  tick(output, 5500);
  assert(owner.attempts.size() == 3);  // periodic refresh
  owner.ready = false;
  tick(output, 6000);
  owner.ready = true;
  tick(output, 6001);
  assert(owner.attempts.size() == 4);  // reassert after owner recovery

  HatchPlusIndicator orphan;
  orphan.write_state(&state);
  tick(orphan, 10000);  // absent owner must not crash or write
  orphan.set_indicator(1);
  assert(orphan.automatic_color() == 0);
}

static void boot() {
  for (bool automatic : {false, true}) {
    for (bool on : {false, true}) {
      HatchPlusIndicator output;
      light::LightState state;
      state.remote_values.state = on;
      output.setup_state(&state);
      output.set_start_automatic(automatic);
      tick(output, 0);
      assert(state.effect == (automatic && on ? "Automatic" : "None"));
      assert(state.calls == unsigned(automatic && on));
      assert(!state.save);
      state.effect = "None";
      tick(output, 5000);
      assert(state.effect == "None");  // boot must not override later manual mode
      assert(state.calls == unsigned(automatic && on));
    }
  }
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "battery") battery();
  else if (scenario == "wifi") wifi_and_lock();
  else if (scenario == "manual") manual_and_automatic();
  else if (scenario == "blinking") blinking();
  else if (scenario == "explicit_off") automatic_explicit_off();
  else if (scenario == "local_status") local_status();
  else if (scenario == "retry") retry();
  else if (scenario == "boot") boot();
  else assert(false);
}
