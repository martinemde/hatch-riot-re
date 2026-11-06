#include "hatch_scenes.h"
#include <cassert>
#include <limits>
using namespace esphome;
using namespace esphome::hatch_scenes;
struct Rig {
  light::LightState light;
  media_player::MediaPlayer player;
  HatchScenes controller;
  Rig() { controller.set_light(&light); controller.set_media_player(&player); controller.setup(); }
  void drain() { for (int i=0; i<100 && !player.queue.empty(); ++i) { player.tick(); if (player.stopping) player.finish_stop(); } assert(player.queue.empty()); }

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

  r.controller.stop(); r.drain();
  assert(!r.light.remote_values.on && !r.player.playing);
}
