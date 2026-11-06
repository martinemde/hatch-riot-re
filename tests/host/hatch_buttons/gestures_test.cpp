#include "hatch_buttons.h"
#include "esphome/core/hal.h"
#include <cassert>
using namespace esphome::hatch_buttons;
struct Rig {
  HatchButtons buttons;
  std::vector<std::string> events, edges;
  Rig() {
    buttons.add_button("touch"); buttons.add_button("bottom");
    buttons.add_on_button_callback([this](auto name,auto gesture){events.push_back(name+":"+gesture);});
    buttons.add_on_press_callback([this](auto name){edges.push_back(name+":press");});
    buttons.add_on_release_callback([this](auto name,auto duration){edges.push_back(name+":"+std::to_string(duration));});
    buttons.add_on_cancel_callback([this](auto name){edges.push_back(name+":cancel");});
  }
  void advance(uint32_t ms) { clock_ms+=ms; buttons.loop(); }
  void click(const std::string &name="touch",uint32_t duration=20) {
    buttons.button_event(name,true); clock_ms+=duration; buttons.button_event(name,false);
  }
};
int main() {
  for (const auto &name : {"touch","bottom"}) {
    for (int count : {1,2,3}) {
      Rig r;
      for (int i=0;i<count;++i) { if (i) r.advance(100); r.click(name); }
      if (count!=3) assert(r.events.empty());
      r.advance(349); if (count!=3) assert(r.events.empty());
      r.advance(1);
      assert(r.events==std::vector<std::string>{std::string(name)+(count==1 ? ":tap" : count==2 ? ":double" : ":triple")});
      r.advance(1000); assert(r.events.size()==1);
      r.buttons.button_event(name,false); assert(r.events.size()==1);
    }
  }
  // Hold fires at the threshold once, not again at release or after a stalled loop.
  Rig hold; hold.buttons.button_event("touch",true);
  hold.advance(799); assert(hold.events.empty());
  hold.advance(1); assert(hold.events==std::vector<std::string>{"touch:hold"});
  hold.advance(10000); hold.buttons.button_event("touch",false);
  assert(hold.events.size()==1 && hold.edges.back()=="touch:10800");
  Rig stale; stale.click("touch",800);
  assert(stale.events==std::vector<std::string>{"touch:hold"});
  Rig below; below.click("touch",799); below.advance(350);
  assert(below.events==std::vector<std::string>{"touch:tap"});
  // Timeout boundary starts a new sequence even without a loop between presses.
  Rig boundary; boundary.click(); clock_ms+=350; boundary.buttons.button_event("touch",true);
  assert(boundary.events==std::vector<std::string>{"touch:tap"});
  clock_ms+=20; boundary.buttons.button_event("touch",false); boundary.advance(350);
  assert(boundary.events.size()==2);
  // Duplicate edges and unknown names do not reset timers or create events.
  Rig duplicate; duplicate.buttons.button_event("unknown",true);
  duplicate.buttons.button_event("touch",true); clock_ms+=400;
  duplicate.buttons.button_event("touch",true); clock_ms+=399;
  duplicate.buttons.button_event("touch",false); duplicate.buttons.button_event("touch",false);
  duplicate.advance(350); assert(duplicate.events==std::vector<std::string>{"touch:tap"});
  assert(duplicate.edges.size()==2);
  // Both duration and idle timers work across uint32 wraparound.
  Rig wrap; wrap.buttons.set_multi_click_ms(200); wrap.buttons.set_hold_ms(1000);
  clock_ms=UINT32_MAX-30; wrap.click(); wrap.advance(199); assert(wrap.events.empty());
  wrap.advance(1); assert(wrap.events.back()=="touch:tap");
  clock_ms=UINT32_MAX-30; wrap.click("touch",1000); assert(wrap.events.back()=="touch:hold");
  Rig concurrent; concurrent.click(); concurrent.click("bottom"); concurrent.click(); concurrent.advance(350);
  assert((concurrent.events==std::vector<std::string>{"touch:double","bottom:tap"}));
  Rig mixed; mixed.click(); mixed.advance(50); mixed.buttons.button_event("touch",true); mixed.advance(800);
  assert((mixed.events==std::vector<std::string>{"touch:tap","touch:hold"}));
  mixed.buttons.button_event("touch",false); mixed.advance(350); assert(mixed.events.size()==2);
  // Lock cancels pending clicks and in-flight presses, even if unlocked before release.
  Rig locked; locked.click(); locked.buttons.set_child_lock(true);
  assert(locked.edges.back()=="touch:cancel"); locked.advance(500);
  locked.buttons.set_child_lock(false); locked.advance(350); assert(locked.events.empty());
  locked.buttons.button_event("touch",true); locked.buttons.set_child_lock(true);
  const auto cancelled_edges=locked.edges.size();
  locked.buttons.set_child_lock(true); assert(locked.edges.size()==cancelled_edges);
  locked.buttons.set_child_lock(false); locked.advance(900); locked.buttons.button_event("touch",false);
  assert(locked.events.empty() && locked.edges.size()==cancelled_edges);
  locked.click(); locked.advance(350); assert(locked.events.back()=="touch:tap");
  Rig blocked; blocked.buttons.set_child_lock(true); blocked.buttons.button_event("touch",true);
  blocked.buttons.set_child_lock(false); blocked.advance(900); blocked.buttons.button_event("touch",false);
  assert(blocked.events.empty() && blocked.edges.empty());
  blocked.buttons.set_allow_when_locked(true); blocked.buttons.set_child_lock(true);
  blocked.click(); blocked.advance(350); assert(blocked.events.back()=="touch:tap");
  // Synchronous callbacks may lock: no hold/release can leak after cancellation.
  Rig callback; callback.click(); callback.advance(50);
  callback.buttons.add_on_button_callback([&](auto,auto){callback.buttons.set_child_lock(true);});
  callback.buttons.button_event("touch",true); callback.advance(800);
  assert(callback.events==std::vector<std::string>{"touch:tap"});
  assert(callback.edges.back()=="touch:cancel"); callback.buttons.set_child_lock(false);
  callback.buttons.button_event("touch",false); callback.advance(350); assert(callback.events.size()==1);
  Rig press_lock; press_lock.buttons.add_on_press_callback([&](auto){press_lock.buttons.set_child_lock(true);});
  press_lock.click(); press_lock.buttons.set_child_lock(false); press_lock.advance(1000);
  assert(press_lock.events.empty());
}
