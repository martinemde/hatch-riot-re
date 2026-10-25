#include "hatch_plus.h"
#include <cassert>
using namespace esphome;
using namespace esphome::hatch_plus;
struct Rig {
  light::LightState light, clock;
  media_player::MediaPlayer player;
  select::Select stock;
  switch_::Switch repeat, show_clock;
  text::Text text;
  hatch_display::HatchDisplay display;
  HatchPlus controller;
  Trigger<std::string, std::string> trigger;
  std::vector<std::string> events;
  Rig() {
    controller.set_light(&light); controller.set_media_player(&player);
    controller.set_stock_sound(&stock); controller.set_loop_switch(&repeat);
    controller.set_clock_switch(&show_clock); controller.set_clock_light(&clock);
    controller.set_text(&text); controller.set_display(&display);
    controller.add_button("ring", "next", "stop");
    controller.add_button("lamp", "toggle_light", "manage_preset");
    trigger.callbacks.push_back([&](auto name, auto gesture){ events.push_back(name+":"+gesture); });
    controller.add_button_trigger(&trigger); controller.setup();
  }
  void advance(uint32_t duration) { clock_ms += duration; controller.loop(); }
  void click(const std::string &name, uint32_t duration) {
    controller.button_event(name, true); clock_ms += duration; controller.button_event(name, false);
  }
};
int main() {
  // Release thresholds work without a single loop iteration during the press.
  for (uint32_t duration : {4999u,5000u,9999u,10000u}) {
    preferences = Preferences{}; Rig r;
    r.click("lamp", duration);
    assert(r.events == std::vector<std::string>{"lamp:hold"});
    assert(r.light.calls == 0);
    if (duration == 5000 || duration == 9999) {
      assert(r.controller.current_favorite() == 0 && r.display.overlays.back() == "SAV0");
      assert(preferences.saves == 1);
    } else {
      assert(r.controller.current_favorite() == -1 && preferences.saves == 0);
      if (duration == 10000) assert(r.display.overlays.back() == "NONE");
      else assert(r.display.overlays.empty());
    }
  }
  preferences = Preferences{}; Rig r;
  r.click("lamp", 20); r.advance(350);
  assert(r.light.calls == 1 && r.events.back() == "lamp:tap");
  assert(r.controller.store_preset(4, "Named scene"));
  assert(r.controller.store_preset(8, "Other"));
  assert(r.controller.apply_preset(4));
  auto saves = preferences.saves;
  r.controller.button_event("lamp", true);
  r.advance(5000); assert(preferences.saves == saves); // no premature save
  r.advance(4999); assert(r.display.overlays.back() == "Named scene");
  r.advance(1); assert(r.display.overlays.back() == "DEL4" && r.display.overlay_duration == 0);
  assert(preferences.saves == saves);
  const auto frames = r.display.overlays.size();
  r.advance(100000); assert(r.display.overlays.size() == frames);
  assert(r.controller.apply_preset(8));
  assert(r.display.overlays.back() == "DEL4"); // still fixed target, not name or newly selected slot
  r.controller.button_event("lamp", false);
  assert(r.display.overlays.back() == "DEL4" && r.display.overlay_duration == 2000);
  assert(!r.controller.play(4) && r.controller.play(8));
  r.click("ring", 20); r.advance(350);
  assert(r.controller.current_favorite() == 8 && r.events.back() == "ring:tap");
  // First free slot is used; full is non-destructive.
  r.click("lamp", 5000); assert(r.display.overlays.back() == "SAV0");
  for (int i = 0; i < 10; ++i) assert(r.controller.store_preset(i, "Keep"));
  saves = preferences.saves;
  r.click("lamp", 9999); assert(r.display.overlays.back() == "FULL" && preferences.saves == saves);
  // Lock cancels preview, pending release, and presses made while disabled.
  r.controller.button_event("lamp", true); r.advance(10000);
  assert(r.display.overlays.back() == "DEL9");
  r.controller.set_child_lock(true); assert(!r.display.overlay_active);
  r.controller.set_child_lock(false); r.advance(20000);
  r.controller.button_event("lamp", false); assert(preferences.saves == saves);
  r.controller.set_child_lock(true); r.controller.button_event("lamp", true);
  r.controller.set_child_lock(false); r.advance(12000); r.controller.button_event("lamp", false);
  assert(preferences.saves == saves);
  r.click("lamp",5000); assert(r.display.overlays.back() == "FULL"); // fresh press works
  // Save/delete thresholds use unsigned elapsed time across millis wraparound.
  preferences = Preferences{}; Rig wrap;
  clock_ms = UINT32_MAX - 2500; wrap.click("lamp",5000);
  assert(wrap.controller.current_favorite() == 0);
  clock_ms = UINT32_MAX - 5000; wrap.click("lamp",10000);
  assert(wrap.display.overlays.back() == "DEL0" && !wrap.controller.play(0));
  // A saved single and subsequent hold stay two classified gestures.
  preferences = Preferences{}; Rig mixed;
  mixed.click("lamp",20); mixed.advance(50); mixed.click("lamp",5000);
  assert((mixed.events == std::vector<std::string>{"lamp:tap", "lamp:hold"}));
  assert(mixed.light.calls == 1 && mixed.display.overlays.back() == "SAV0");
  // Synchronous automation lock callbacks cancel the current physical press/release,
  // even if the callback unlocks immediately or unlock happens before release.
  for (bool late_press : {false, true}) {
    for (bool immediate_unlock : {false, true}) {
      preferences = Preferences{}; Rig locked_callback;
      locked_callback.trigger.callbacks.push_back([&](auto, auto gesture) {
        if (gesture != "tap") return;
        locked_callback.controller.set_child_lock(true);
        if (immediate_unlock) locked_callback.controller.set_child_lock(false);
      });
      locked_callback.click("lamp",20);
      clock_ms += late_press ? 350 : 50;
      locked_callback.controller.button_event("lamp",true);
      if (late_press) locked_callback.controller.set_child_lock(false);
      clock_ms += 5000;
      locked_callback.controller.button_event("lamp",false);
      assert(locked_callback.controller.current_favorite() == -1 && preferences.saves == 0);
      assert(locked_callback.events == std::vector<std::string>{"lamp:tap"});
      locked_callback.controller.set_child_lock(false);
      locked_callback.click("lamp",5000);
      assert(locked_callback.display.overlays.back() == "SAV0");
    }
  }
  // Opt-in is the mapping itself; old explicit hold mappings keep their behavior.
  preferences = Preferences{}; Rig old;
  old.controller.add_button("legacy", "none", "save");
  old.click("legacy",5000); assert(old.display.overlays.empty() && preferences.saves == 0);
}
