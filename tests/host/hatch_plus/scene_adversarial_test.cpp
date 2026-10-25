#include "hatch_plus.h"
#include <cassert>
#include <iostream>
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
    preferences=Preferences{};
    controller.set_light(&light); controller.set_media_player(&player);
    controller.set_stock_sound(&stock); controller.set_loop_switch(&repeat);
    controller.set_clock_switch(&show_clock); controller.set_clock_light(&clock);
    controller.set_text(&text); controller.set_display(&display);
    trigger.callbacks.push_back([&](auto name, auto gesture){ events.push_back(name+":"+gesture); });
    controller.add_button_trigger(&trigger); controller.setup();
  }
  void click(const std::string &name, uint32_t duration) {
    controller.button_event(name,true); clock_ms+=duration; controller.button_event(name,false);
  }
};
int main(int argc, char **argv) {
  assert(argc==2); const std::string scenario=argv[1];
  Rig r;
  if (scenario=="disabled") {
    r.controller.add_button("touch","none","none");
    assert(r.controller.store_preset(3,"Keep"));
    const int saves=preferences.saves;
    r.click("touch",20); clock_ms+=350; r.controller.loop();
    assert(r.controller.current_favorite()==3 && saves==preferences.saves);
    assert(r.player.queue.empty() && r.light.calls==0);
    assert(r.controller.apply_preset(3));
  } else if (scenario=="allow_locked") {
    r.controller.add_button("lamp","toggle_light","manage_preset");
    r.controller.set_allow_when_locked(true); r.controller.set_child_lock(true);
    r.click("lamp",5000);
    assert(r.controller.current_favorite()==0 && r.display.overlays.back()=="SAV0");
  } else if (scenario=="reentrant_lock") {
    r.controller.add_button("lamp","toggle_light","manage_preset");
    r.trigger.callbacks.push_back([&](auto,auto gesture){if(gesture=="tap")r.controller.set_child_lock(true);});
    r.click("lamp",20); clock_ms+=50; r.click("lamp",5000);
    assert(r.controller.get_child_lock());
    assert(r.controller.current_favorite()==-1 && preferences.saves==0);
    assert(r.events==std::vector<std::string>{"lamp:tap"});
  } else if (scenario=="reentrant_lock_unlock") {
    r.controller.add_button("lamp","toggle_light","manage_preset");
    r.trigger.callbacks.push_back([&](auto,auto gesture){
      if(gesture=="tap") {r.controller.set_child_lock(true);r.controller.set_child_lock(false);}
    });
    r.click("lamp",20); clock_ms+=50; r.click("lamp",5000);
    assert(!r.controller.get_child_lock());
    assert(r.controller.current_favorite()==-1 && preferences.saves==0);
    assert(r.events==std::vector<std::string>{"lamp:tap"});
  } else if (scenario=="late_press_lock" || scenario=="late_press_lock_unlock") {
    r.controller.add_button("lamp","toggle_light","manage_preset");
    r.trigger.callbacks.push_back([&](auto,auto gesture){
      if(gesture=="tap") {r.controller.set_child_lock(true);if(scenario=="late_press_lock_unlock")r.controller.set_child_lock(false);}
    });
    r.click("lamp",20); clock_ms+=350;
    r.controller.button_event("lamp",true);
    assert(r.controller.get_child_lock()==(scenario=="late_press_lock"));
    r.controller.set_child_lock(false);
    clock_ms+=5000; r.controller.button_event("lamp",false);
    assert(r.controller.current_favorite()==-1 && preferences.saves==0);
  } else { assert(false); }
  std::cout << scenario << " passed\n";
}
