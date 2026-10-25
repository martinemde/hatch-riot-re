#include "hatch_plus.h"
#include <cassert>
#include <limits>
using namespace esphome;
using namespace esphome::hatch_plus;
struct Rig {
  light::LightState light;
  media_player::MediaPlayer player;
  HatchPlus controller;
  Rig() { controller.set_light(&light); controller.set_media_player(&player); controller.setup(); }
  void drain() { for (int i=0; i<100 && !player.queue.empty(); ++i) { player.tick(); if (player.stopping) player.finish_stop(); } assert(player.queue.empty()); }
  void edge(const std::string &name, uint32_t duration) {
    controller.button_event(name, true); clock_ms += duration; controller.button_event(name, false);
    clock_ms += 350; controller.loop();
  }
};
int main() {
  preferences = Preferences{};
  Rig r;
  assert(r.controller.current_favorite() == -1);
  assert(!r.controller.save_current() && !r.controller.next() && !r.controller.previous());
  assert(!r.controller.capture(-1) && !r.controller.play(10));
  assert(!r.controller.configure_favorite(0,"file:///bad",false));
  assert(r.controller.configure_favorite(0,"https://example.org/rain.mp3",true));
  r.light.remote_values = {true,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f}; r.player.volume=0.8f;
  assert(r.controller.capture(0));
  assert(r.controller.configure_favorite(3,"hatch-library:///my rain.mp3",false));
  assert(r.controller.configure_favorite(7,"",false));
  assert(r.controller.play(0)); r.drain();
  assert(r.light.remote_values.on && r.light.remote_values.red==0.2f);
  assert(r.player.uri=="https://example.org/rain.mp3" && r.player.repeat && r.player.volume==0.8f);
  assert(r.controller.next());
  r.player.tick(); // repeat-off is queued before replacement URI
  r.player.tick(); assert(r.player.stopping && r.player.starts==1);
  r.player.tick(); assert(r.player.starts==1); // cannot start while the old source is busy
  r.player.finish_stop(); r.drain();
  assert(r.player.uri=="hatch-library:///my rain.mp3" && !r.player.repeat);
  assert(r.controller.next()); r.drain(); assert(!r.player.playing); // light-only stops sound
  assert(r.controller.next()); r.drain(); assert(r.controller.current_favorite()==0);
  assert(r.controller.previous()); r.drain(); assert(r.controller.current_favorite()==7);
  Rig restored;
  assert(restored.controller.current_favorite()==7);
  assert(restored.player.queue.empty() && restored.light.calls==0); // reboot does not play

  // Sync failure must not become a false success on repeated play of the same selection.
  preferences.sync_ok=false;
  assert(!r.controller.play(3));
  int writes=preferences.saves;
  assert(!r.controller.play(3) && preferences.saves==writes+1);
  preferences.sync_ok=true;
  assert(r.controller.play(3));
  writes=preferences.saves;
  assert(r.controller.play(3) && preferences.saves==writes); // no needless flash writes
  Rig selected; assert(selected.controller.current_favorite()==3);
  preferences.save_ok=false;
  r.player.volume=0.25f;
  assert(!r.controller.save_current());
  assert(!r.controller.play(3)); // dirty capture is not reported as persisted
  preferences.save_ok=true;
  assert(r.controller.play(3));
  Rig saved; assert(saved.controller.play(3)); assert(saved.player.volume==0.25f);
  r.player.volume=std::numeric_limits<float>::quiet_NaN();
  writes=preferences.saves;
  assert(!r.controller.capture(3) && preferences.saves==writes);
  r.player.volume=0.5f;
  assert(r.controller.erase(3)); assert(r.controller.current_favorite()==-1);
  Rig erased; assert(erased.controller.current_favorite()==-1 && !erased.controller.play(3));

  r.drain();
  r.controller.add_button("touch","next","stop");
  r.controller.add_button("bottom","previous","save");
  r.controller.add_button("light","toggle_light","none");
  r.controller.add_button("brighter","brightness_up","none");
  r.controller.add_button("dimmer","brightness_down","none");
  r.controller.add_button("up","volume_up","none");
  r.controller.add_button("down","volume_down","none");
  Trigger<std::string,std::string> trigger; std::vector<std::string> gestures;
  trigger.callbacks.push_back([&](auto name,auto gesture){gestures.push_back(name+":"+gesture);});
  r.controller.add_button_trigger(&trigger);
  r.controller.button_event("touch",false); r.controller.button_event("unknown",true);
  assert(gestures.empty());
  r.controller.button_event("touch",true); clock_ms+=400;
  r.controller.button_event("touch",true); clock_ms+=399;
  r.controller.button_event("touch",false);
  clock_ms+=350; r.controller.loop();
  assert(gestures.back()=="touch:tap" && r.controller.current_favorite()==0);
  r.edge("bottom",10); assert(r.controller.current_favorite()==7);
  r.edge("bottom",800); assert(gestures.back()=="bottom:hold");
  r.edge("touch",800); r.drain(); assert(!r.light.remote_values.on && !r.player.playing);
  r.edge("light",1); assert(r.light.remote_values.on);
  r.light.remote_values.brightness=0.95f;
  r.edge("brighter",1); assert(r.light.remote_values.brightness==1.0f);
  r.light.remote_values.brightness=0.05f;
  r.edge("dimmer",1); assert(r.light.remote_values.brightness==0 && !r.light.remote_values.on);
  r.player.volume=0.95f; r.edge("up",1); assert(r.player.volume==1.0f);
  r.player.volume=0.05f; r.edge("down",1); assert(r.player.volume==0);
  auto count=gestures.size();
  r.controller.button_event("light",true); r.controller.set_child_lock(true);
  r.controller.button_event("light",false); assert(gestures.size()==count);
  r.controller.set_allow_when_locked(true); r.edge("light",1); assert(gestures.size()==count+1);
  r.controller.set_child_lock(false);
  clock_ms=UINT32_MAX-100; r.edge("touch",800); assert(gestures.back()=="touch:hold");

  // Real controller routing with a passive player double; backend mute/PCM is tested separately.
  preferences = Preferences{};
  Rig volume;
  volume.controller.add_button("up","volume_up","none");
  volume.controller.add_button("down","volume_down","none");
  volume.controller.add_button("save","save","none");
  Trigger<std::string,std::string> volume_trigger;
  int volume_gestures=0;
  volume_trigger.callbacks.push_back([&](auto,auto){++volume_gestures;});
  volume.controller.add_button_trigger(&volume_trigger);
  const std::string loop_uri="hatch-library:///rain.wav";
  assert(volume.controller.configure_favorite(0,loop_uri,true));
  assert(volume.controller.capture(0));
  assert(volume.controller.play(0)); volume.drain();
  const int starts=volume.player.starts;
  // Match the Plus configuration's speaker_source default: five percentage points.
  volume.edge("up",1); assert(volume.player.volume==0.55f);
  volume.edge("down",1); assert(volume.player.volume==0.5f);
  const int volume_writes=preferences.saves;
  auto unchanged_playback = [&]() {
    assert(volume.player.playing && volume.player.repeat && !volume.player.stopping);
    assert(volume.player.uri==loop_uri && volume.player.starts==starts && volume.player.queue.empty());
  };
  for (int i=0; i<30; ++i) { volume.edge("up",1); unchanged_playback(); }
  assert(volume.player.volume==1.0f);
  for (int i=0; i<30; ++i) { volume.edge("down",1); unchanged_playback(); }
  assert(volume.player.volume==0.0f);
  volume.edge("up",1); assert(volume.player.volume>0); unchanged_playback();
  assert(preferences.saves==volume_writes); // Volume changes alone never capture favorites.

  // Lock applies at physical release, not to the direct media-player/API path.
  const float before_lock=volume.player.volume;
  const int before_gestures=volume_gestures;
  volume.controller.button_event("up",true);
  volume.controller.set_child_lock(true);
  volume.controller.button_event("up",false);
  volume.edge("down",1);
  assert(volume.player.volume==before_lock && volume_gestures==before_gestures);
  volume.player.make_call().set_volume(0.65f).perform();
  assert(volume.player.volume==0.65f); unchanged_playback();
  volume.player.make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_VOLUME_DOWN).perform();
  assert(volume.player.volume<0.65f); unchanged_playback();
  volume.controller.set_allow_when_locked(true);
  volume.edge("up",1);
  assert(volume_gestures==before_gestures+1 && volume.player.volume==0.65f);
  volume.controller.set_allow_when_locked(false);
  volume.controller.set_child_lock(false);


  // Adjustment holds wait for hold_time, then step at 150 ms without replay on release.
  preferences = Preferences{};
  Rig repeat;
  for (const auto &mapping : std::vector<std::pair<std::string, std::string>>{
           {"up", "volume_up"}, {"down", "volume_down"},
           {"brighter", "brightness_up"}, {"dimmer", "brightness_down"}}) {
    repeat.controller.add_button(mapping.first, mapping.second, "none");
  }
  Trigger<std::string,std::string> repeat_trigger;
  std::vector<std::string> repeat_gestures;
  repeat_trigger.callbacks.push_back([&](auto name,auto gesture){repeat_gestures.push_back(name+":"+gesture);});
  repeat.controller.add_button_trigger(&repeat_trigger);
  assert(repeat.controller.configure_favorite(0,loop_uri,true));
  assert(repeat.controller.capture(0));
  assert(repeat.controller.play(0)); repeat.drain();
  const int repeat_writes=preferences.saves, repeat_starts=repeat.player.starts;
  for (const std::string name : {"up", "down", "brighter", "dimmer"}) {
    repeat.player.volume=0.5f; repeat.light.remote_values.brightness=0.5f;
    auto value = [&]() { return name=="up" || name=="down" ? repeat.player.volume : repeat.light.remote_values.brightness; };
    const float step = name=="up" ? 0.05f : name=="down" ? -0.05f : name=="brighter" ? 0.1f : -0.1f;
    clock_ms=UINT32_MAX-400; // initial delay and cadence work through timer wraparound
    repeat.controller.button_event(name,true);
    clock_ms+=799; repeat.controller.loop(); assert(value()==0.5f);
    repeat.controller.button_event(name,true); // duplicate press does not restart the wait
    clock_ms+=1; repeat.controller.loop(); assert(value()==0.5f+step);
    repeat.controller.loop(); assert(value()==0.5f+step);
    clock_ms+=149; repeat.controller.loop(); assert(value()==0.5f+step);
    clock_ms+=1; repeat.controller.loop(); assert(value()==(0.5f+step)+step);
    assert(repeat_gestures.empty() || repeat_gestures.back()!=name+":hold");
    const float before_release=value();
    repeat.controller.button_event(name,false); assert(value()==before_release);
    assert(repeat_gestures.back()==name+":hold");
    clock_ms+=1000; repeat.controller.loop(); assert(value()==before_release);
    const auto gestures_after_release=repeat_gestures.size();
    repeat.controller.button_event(name,false); assert(repeat_gestures.size()==gestures_after_release);
    // A new short press still produces exactly one tap step on release.
    repeat.player.volume=0.5f; repeat.light.remote_values.brightness=0.5f;
    repeat.edge(name,20); assert(value()==0.5f+step && repeat_gestures.back()==name+":tap");
    repeat.controller.button_event(name,true);
    for (int i=0; i<30; ++i) { clock_ms+=800; repeat.controller.loop(); }
    assert(value()==(step>0 ? 1.0f : 0.0f));
    repeat.controller.button_event(name,false);
  }
  assert(repeat.player.playing && repeat.player.repeat && repeat.player.uri==loop_uri);
  assert(repeat.player.starts==repeat_starts && repeat.player.queue.empty());
  assert(preferences.saves==repeat_writes);

  // Lock suppresses ongoing repeats and release callbacks, with the existing opt-in override.
  repeat.player.volume=0.5f;
  repeat.controller.button_event("up",true);
  clock_ms+=800; repeat.controller.loop(); assert(repeat.player.volume==0.55f);
  repeat.controller.set_child_lock(true);
  clock_ms+=1000; repeat.controller.loop(); assert(repeat.player.volume==0.55f);
  const auto locked_gestures=repeat_gestures.size();
  repeat.controller.button_event("up",false);
  assert(repeat_gestures.size()==locked_gestures);
  repeat.controller.set_allow_when_locked(true);
  repeat.controller.button_event("down",true);
  clock_ms+=800; repeat.controller.loop(); assert(repeat.player.volume==0.5f);
  repeat.controller.button_event("down",false);
  assert(repeat_gestures.back()=="down:hold");
  repeat.controller.set_child_lock(false);

  // Custom hold mappings and favorite controls remain release-only, never repeated.
  repeat.controller.add_button("custom","volume_up","save");
  repeat.controller.add_button("favorite","next","stop");
  for (const std::string name : {"custom", "favorite"}) {
    repeat.controller.button_event(name,true);
    clock_ms+=5000; repeat.controller.loop(); repeat.controller.loop();
    assert(preferences.saves==repeat_writes+(name=="favorite" ? 1 : 0) && repeat.player.queue.empty());
    assert(repeat.player.playing);
    repeat.controller.button_event(name,false);
    if (name=="custom") assert(preferences.saves==repeat_writes+1);
  }
  repeat.drain(); assert(!repeat.player.playing);

  // Delay follows the configured hold_time; a slow loop issues only one step.
  repeat.controller.set_hold_ms(1000);
  repeat.player.volume=0.5f;
  repeat.controller.button_event("up",true);
  clock_ms+=999; repeat.controller.loop(); assert(repeat.player.volume==0.5f);
  clock_ms+=1; repeat.controller.loop(); assert(repeat.player.volume==0.55f);
  clock_ms+=5000; repeat.controller.loop(); assert(repeat.player.volume==0.6f);
  repeat.controller.button_event("up",false);


  // The real night light defaults to a 1 s fade. Repeats must update output now,
  // not merely move remote_values while restarting that fade every 150 ms.
  Rig fading;
  fading.controller.add_button("brighter","brightness_up","none");
  fading.controller.add_button("dimmer","brightness_down","none");
  fading.light.remote_values.brightness=0.5f;
  fading.light.finish_transition();
  fading.controller.button_event("brighter",true);
  clock_ms+=799; fading.controller.loop();
  assert(fading.light.current_values.brightness==0.5f);
  clock_ms+=1; fading.controller.loop();
  assert(fading.light.current_values.brightness==0.6f);
  clock_ms+=150; fading.controller.loop();
  const float lit=fading.light.current_values.brightness;
  assert(lit==0.6f+0.1f && lit==fading.light.remote_values.brightness);
  fading.controller.button_event("brighter",false);
  assert(fading.light.current_values.brightness==lit);
  clock_ms+=1000; fading.controller.loop(); fading.light.finish_transition();
  assert(fading.light.current_values.brightness==lit);
  fading.edge("dimmer",1);
  assert(fading.light.current_values.brightness==lit-0.1f);

  // Capture zero and full scale through the physical save action, then restore across setup.
  for (float boundary : {0.0f, 1.0f}) {
    volume.player.make_call().set_volume(boundary).perform();
    volume.edge("save",1);
    unchanged_playback();
    Rig rebooted;
    assert(rebooted.controller.current_favorite()==0);
    assert(rebooted.player.queue.empty() && rebooted.player.starts==0);
    assert(rebooted.controller.play(0)); rebooted.drain();
    assert(rebooted.player.volume==boundary && rebooted.player.repeat && rebooted.player.uri==loop_uri);
  }
}
