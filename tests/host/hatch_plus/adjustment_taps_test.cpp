#include "hatch_plus.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
using namespace esphome;
using namespace esphome::hatch_plus;

struct Rig {
  light::LightState light;
  media_player::MediaPlayer player;
  HatchPlus controller;
  Trigger<std::string, std::string> trigger;
  std::vector<std::string> events;
  std::string action;
  explicit Rig(const std::string &action, const std::string &hold = "none") : action(action) {
    preferences = Preferences{};
    light.remote_values.brightness = 0.5f;
    controller.set_light(&light); controller.set_media_player(&player); controller.setup();
    // Deliberately use a custom physical name: adjustment behavior follows the mapping.
    controller.add_button("adjust", action, hold);
    trigger.callbacks.push_back([&](auto name, auto gesture) { events.push_back(name + ":" + gesture); });
    controller.add_button_trigger(&trigger);
  }
  float value() const { return action.find("volume") == 0 ? player.volume : light.remote_values.brightness; }
  float step() const {
    return (action.find("volume") == 0 ? 0.05f : 0.1f) * (action.find("_up") != std::string::npos ? 1 : -1);
  }
  void expect_steps(int count) const { assert(std::fabs(value() - (0.5f + count * step())) < 0.00001f); }
  void advance(uint32_t duration) { clock_ms += duration; controller.loop(); }
  void press() { controller.button_event("adjust", true); }
  void release() { controller.button_event("adjust", false); }
  void click(uint32_t duration = 20) { press(); clock_ms += duration; release(); }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  for (const std::string action : {"volume_up", "volume_down", "brightness_up", "brightness_down"}) {
    if (scenario == "rapid") {
      for (int count : {1, 2, 3}) {
        Rig r(action);
        for (int i = 0; i < count; ++i) {
          if (i) r.advance(100); // within the 350 ms multi-click window
          r.press(); r.expect_steps(i);
          clock_ms += 20; r.release();
          r.expect_steps(i + 1); // no loop or click timeout before the action
          assert(r.events == std::vector<std::string>(i + 1, "adjust:tap"));
          r.release(); r.expect_steps(i + 1); // duplicate edges do not add taps
          assert(r.events.size() == static_cast<size_t>(i + 1));
        }
        r.advance(1000); r.expect_steps(count);
        assert(r.events == std::vector<std::string>(count, "adjust:tap"));
      }
      Rig near_hold(action);
      near_hold.click(799); near_hold.expect_steps(1);
      assert(near_hold.events == std::vector<std::string>{"adjust:tap"});
    } else if (scenario == "repeat") {
      Rig r(action);
      clock_ms = UINT32_MAX - 400;
      r.press(); r.advance(799); r.expect_steps(0); assert(r.events.empty());
      r.press(); // duplicate press must not reset the hold timer
      r.advance(1); r.expect_steps(1); assert(r.events.empty());
      r.advance(149); r.expect_steps(1);
      r.advance(1); r.expect_steps(2); assert(r.events.empty());
      r.controller.loop(); r.expect_steps(2);
      r.advance(1000); r.expect_steps(3); // no catch-up burst after a stalled loop
      r.release(); r.expect_steps(3);
      assert(r.events == std::vector<std::string>{"adjust:hold"});
      r.release(); r.advance(1000); r.expect_steps(3);
      assert(r.events == std::vector<std::string>{"adjust:hold"});
    } else if (scenario == "explicit_hold") {
      Rig short_press(action, "toggle_light");
      short_press.click(); short_press.expect_steps(1);
      assert(short_press.events == std::vector<std::string>{"adjust:tap"});
      short_press.advance(350); short_press.expect_steps(1);
      assert(short_press.events.size() == 1);
      Rig hold(action, "toggle_light");
      hold.press(); hold.advance(800); hold.advance(150); hold.advance(3000);
      hold.expect_steps(0); assert(hold.light.calls == 0 && hold.events.empty());
      hold.release(); hold.expect_steps(0);
      assert(hold.light.calls == 1 && hold.light.remote_values.on);
      assert(hold.events == std::vector<std::string>{"adjust:hold"});
      hold.advance(1000); assert(hold.light.calls == 1 && hold.events.size() == 1);
    } else if (scenario == "lock") {
      Rig r(action);
      r.controller.set_child_lock(true);
      r.click(); r.advance(1000); r.expect_steps(0); assert(r.events.empty());
      r.controller.set_child_lock(false); r.advance(1000); r.expect_steps(0);
      r.press(); r.controller.set_child_lock(true); r.controller.set_child_lock(false);
      clock_ms += 20; r.release(); r.expect_steps(0); assert(r.events.empty());
      r.controller.set_child_lock(true); r.press(); r.controller.set_child_lock(false);
      r.advance(1000); r.release(); r.expect_steps(0); assert(r.events.empty());
      r.click(); r.expect_steps(1); assert(r.events == std::vector<std::string>{"adjust:tap"});
      r.press(); r.advance(800); r.expect_steps(2);
      r.controller.set_child_lock(true); r.advance(1000); r.release();
      r.controller.set_child_lock(false); r.advance(1000); r.expect_steps(2);
      assert(r.events == std::vector<std::string>{"adjust:tap"});
      Rig allowed(action);
      allowed.controller.set_allow_when_locked(true); allowed.controller.set_child_lock(true);
      allowed.click(); allowed.expect_steps(1);
      assert(allowed.events == std::vector<std::string>{"adjust:tap"});
      allowed.press(); allowed.advance(800); allowed.advance(150); allowed.release(); allowed.expect_steps(3);
      assert((allowed.events == std::vector<std::string>{"adjust:tap", "adjust:hold"}));
    } else { assert(false); }
  }
}
