#include "hatch_plus.h"
#include <cassert>
#include <string>
#include <vector>
using namespace esphome;
using namespace esphome::hatch_plus;
struct Rig {
  light::LightState light;
  media_player::MediaPlayer player;
  HatchPlus controller;
  Trigger<std::string,std::string> trigger;
  std::vector<std::string> events;
  Rig() {
    controller.set_light(&light); controller.set_media_player(&player); controller.setup();
    trigger.callbacks.push_back([&](auto name,auto gesture){events.push_back(name+":"+gesture);});
    controller.add_button_trigger(&trigger);
  }
  void advance(uint32_t ms) { clock_ms+=ms; controller.loop(); }
  void click(const std::string &name, uint32_t duration=20) {
    controller.button_event(name,true); clock_ms+=duration; controller.button_event(name,false);
  }
};
int main() {
  const std::vector<std::string> names={"touch","bottom","night_light","volume_up","volume_down","brightness_up","brightness_down"};
  // Every mapped physical control reports each gesture exactly once. A local tap mapping
  // with an observable effect proves multi-clicks do not invoke even one tap action.
  for (const auto &name : names) {
    for (int count : {1,2,3}) {
      Rig r; r.controller.add_button(name,"toggle_light","stop");
      for (int i=0;i<count;++i) { if (i) r.advance(100); r.click(name); }
      if (count!=3) assert(r.events.empty() && r.light.calls==0);
      r.advance(349); if (count!=3) assert(r.events.empty());
      r.advance(1);
      assert(r.events==std::vector<std::string>{name+(count==1 ? ":tap" : count==2 ? ":double" : ":triple")});
      assert(r.light.calls==(count==1 ? 1 : 0));
      r.advance(1000); assert(r.events.size()==1);
      r.controller.button_event(name,false); assert(r.events.size()==1);
    }
    Rig r; r.controller.add_button(name,"toggle_light","stop");
    r.click(name,800); assert(r.events==std::vector<std::string>{name+":hold"});
    assert(r.light.calls==1 && !r.light.remote_values.on);
    r.advance(1000); assert(r.events.size()==1);
  }
  // Multi-clicks do not select/play/capture a favorite, even on the touch/bottom paths.
  preferences=Preferences{};
  Rig native;
  assert(native.controller.configure_favorite(0,"",false));
  assert(native.controller.configure_favorite(1,"",false));
  assert(native.controller.capture(0));
  native.controller.add_button("touch","next","stop");
  native.controller.add_button("bottom","previous","save");
  const int writes=preferences.saves;
  for (auto name : {"touch","bottom"}) {
    for (int count : {2,3}) {
      for (int i=0;i<count;++i) { if (i) native.advance(100); native.click(name); }
      native.advance(350);
      assert(native.controller.current_favorite()==0 && preferences.saves==writes);
      assert(native.player.queue.empty() && native.light.calls==0);
    }
  }
  // Boundary is exclusive: a new press exactly at the timeout starts a new sequence,
  // even if no loop ran while idle. Repeated edges do not reset either timer.
  Rig boundary; boundary.controller.add_button("touch","toggle_light","stop");
  boundary.click("touch"); clock_ms+=350;
  boundary.controller.button_event("touch",true);
  assert(boundary.events==std::vector<std::string>{"touch:tap"});
  clock_ms+=20; boundary.controller.button_event("touch",false);
  boundary.advance(350); assert(boundary.events.size()==2 && boundary.light.calls==2);
  boundary.events.clear();
  boundary.controller.button_event("touch",true); clock_ms+=400;
  boundary.controller.button_event("touch",true); clock_ms+=399;
  boundary.controller.button_event("touch",false); boundary.controller.button_event("touch",false);
  boundary.advance(350); assert(boundary.events==std::vector<std::string>{"touch:tap"});
  // Timer arithmetic remains correct across wraparound, and timings are configurable.
  Rig wrap; wrap.controller.add_button("touch","toggle_light","stop");
  wrap.controller.set_multi_click_ms(200); wrap.controller.set_hold_ms(1000);
  clock_ms=UINT32_MAX-30;
  wrap.click("touch"); wrap.advance(199); assert(wrap.events.empty());
  wrap.advance(1); assert(wrap.events.back()=="touch:tap");
  wrap.click("touch",999); wrap.advance(200); assert(wrap.events.back()=="touch:tap");
  wrap.click("touch",1000); assert(wrap.events.back()=="touch:hold");
  // Buttons have independent pending sequences.
  Rig concurrent;
  concurrent.controller.add_button("touch","none","none");
  concurrent.controller.add_button("bottom","none","none");
  concurrent.click("touch"); concurrent.click("bottom"); concurrent.click("touch");
  concurrent.advance(350);
  assert((concurrent.events==std::vector<std::string>{"touch:double","bottom:tap"}));
  // Lock cancels pending clicks permanently: unlocking cannot replay old actions.
  Rig locked; locked.controller.add_button("touch","toggle_light","stop");
  locked.click("touch"); locked.controller.set_child_lock(true);
  locked.advance(500); locked.controller.set_child_lock(false); locked.advance(1);
  assert(locked.events.empty() && locked.light.calls==0);
  locked.controller.set_child_lock(true); locked.click("touch");
  locked.controller.set_child_lock(false); locked.advance(350); assert(locked.events.empty());
  locked.controller.set_child_lock(true); locked.controller.set_allow_when_locked(true);
  locked.click("touch"); locked.advance(350); assert(locked.events.back()=="touch:tap");
  // A short click followed by a hold is two separate gestures, never double+tap.
  Rig mixed; mixed.controller.add_button("touch","toggle_light","stop");
  mixed.click("touch"); mixed.advance(50); mixed.click("touch",800);
  assert((mixed.events==std::vector<std::string>{"touch:tap","touch:hold"}));
  mixed.advance(350); assert(mixed.events.size()==2);
  // Hold-repeat behavior is still local, starts at 800 ms, and emits just hold on release.
  Rig repeat; repeat.controller.add_button("volume_up","volume_up","none");
  repeat.controller.button_event("volume_up",true);
  repeat.advance(799); assert(repeat.player.volume==0.5f && repeat.events.empty());
  repeat.advance(1); assert(repeat.player.volume==0.55f && repeat.events.empty());
  repeat.advance(150); assert(repeat.player.volume==0.6f && repeat.events.empty());
  repeat.controller.button_event("volume_up",false);
  assert(repeat.events==std::vector<std::string>{"volume_up:hold"});
  repeat.advance(350); assert(repeat.events.size()==1 && repeat.player.volume==0.6f);
}
