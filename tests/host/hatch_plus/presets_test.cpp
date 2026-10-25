#include "hatch_plus.h"
#include <cassert>
#include <limits>
using namespace esphome;
using namespace esphome::hatch_plus;
static_assert(sizeof(LegacyFavorite)==288);
static_assert(sizeof(LegacyFavoriteStore)==2312);
struct Rig {
  light::LightState light, clock;
  media_player::MediaPlayer player;
  select::Select stock;
  switch_::Switch repeat, show_clock;
  text::Text text;
  hatch_display::HatchDisplay display;
  HatchPlus controller;
  Rig() {
    controller.set_light(&light); controller.set_media_player(&player);
    controller.set_stock_sound(&stock); controller.set_loop_switch(&repeat);
    controller.set_clock_switch(&show_clock); controller.set_clock_light(&clock);
    controller.set_text(&text); controller.set_display(&display);
    repeat.on_control = [&](bool value) { player.make_call().set_command(value ? media_player::MEDIA_PLAYER_COMMAND_REPEAT_ONE : media_player::MEDIA_PLAYER_COMMAND_REPEAT_OFF).perform(); };
    stock.on_control = [&](const std::string &value) {
      if(value=="None") { player.make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).perform(); return; }
      repeat.turn_on();
      player.make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_REPEAT_ONE).perform();
      player.make_call().set_media_url("hatch-library:///"+value+".wav").perform();
    };
    show_clock.on_control = [&](bool) { display.underlying = "clock"; };
    text.on_control = [&](const std::string &value) { display.underlying = value; };
    controller.setup();
  }
  void drain() { for(int i=0; i<100 && !player.queue.empty(); ++i) { player.tick(); if(player.stopping)player.finish_stop(); } assert(player.queue.empty()); }
};
int main() {
  preferences = Preferences{};
  Rig r;
  assert(!r.controller.apply_preset(0));
  assert(!r.controller.store_preset(-1,"bad") && !r.controller.store_preset(10,"bad"));
  r.stock.state="Rain"; r.repeat.state=false; r.show_clock.state=false;
  r.text.state=std::string(128,'t'); r.player.volume=0.0f;
  r.light.remote_values={true,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f};
  r.clock.remote_values.on=false; r.clock.remote_values.brightness=0.2f;
  assert(r.controller.store_preset(9,"Bedtime"));
  assert(r.display.overlays.back()=="SAV9");
  Rig restored;
  assert(restored.player.queue.empty() && restored.light.calls==0 && restored.clock.calls==0);
  assert(restored.display.overlays.empty());
  assert(restored.controller.apply_preset(9)); restored.drain();
  assert(restored.stock.state=="Rain" && !restored.repeat.state && !restored.player.repeat);
  assert(restored.player.volume==0 && restored.player.uri=="hatch-library:///Rain.wav");
  assert(!restored.clock.remote_values.on && restored.clock.remote_values.brightness==0.2f);
  assert(!restored.show_clock.state && restored.text.state==std::string(128,'t'));
  assert(restored.light.remote_values.on && restored.light.remote_values.brightness==0.6f);
  assert(restored.display.underlying==std::string(128,'t') && restored.display.overlays.back()=="Bedtime");
  // Existing play/next mappings can use shared named slots, with no new button routing.
  const auto overlays=restored.display.overlays.size();
  restored.text.state="new";
  assert(restored.controller.play(9)); restored.drain();
  assert(restored.text.state==std::string(128,'t') && restored.display.overlays.size()==overlays+1);
  assert(restored.controller.next()); restored.drain();
  assert(restored.player.uri=="hatch-library:///Rain.wav" && restored.display.overlays.back()=="Bedtime");
  // A legacy capture on a named slot refreshes the complete snapshot and retains its name.
  restored.text.state="updated";
  restored.stock.state="Ocean";
  assert(restored.controller.capture(9));
  restored.text.state="changed again";
  assert(restored.controller.apply_preset(9)); restored.drain();
  assert(restored.text.state=="updated" && restored.display.overlays.back()=="Bedtime");
  // Boundary strings, fallback label, None/stop, and brightness/volume limits.
  r.stock.state="None"; r.repeat.state=true; r.show_clock.state=true;
  r.text.state=""; r.player.volume=1.0f;
  r.clock.remote_values.on=true; r.clock.remote_values.brightness=1;
  assert(r.controller.store_preset(8," \t"));
  assert(r.controller.apply_preset(8)); r.drain();
  assert(r.display.overlays.back()=="FAV8" && !r.player.playing && r.player.repeat);
  assert(r.player.volume==1 && r.clock.remote_values.brightness==1);
  assert(r.controller.store_preset(0,std::string(32,'n')));
  auto writes=preferences.saves;
  assert(!r.controller.store_preset(0,std::string(33,'n')) && preferences.saves==writes);
  r.text.state=std::string(129,'x');
  assert(!r.controller.store_preset(0,"bad") && preferences.saves==writes);
  r.text.state="ok";
  r.clock.remote_values.brightness=std::numeric_limits<float>::quiet_NaN();
  assert(!r.controller.store_preset(0,"bad") && preferences.saves==writes);
  r.clock.remote_values.brightness=0;
  const auto saved_overlays=r.display.overlays.size();
  preferences.save_ok=false;
  assert(!r.controller.store_preset(2,"failed") && r.display.overlays.size()==saved_overlays);
  preferences.save_ok=true; preferences.sync_ok=false;
  assert(!r.controller.store_preset(2,"retry") && r.display.overlays.size()==saved_overlays);
  assert(!r.controller.apply_preset(2) && r.display.overlays.size()==saved_overlays);
  preferences.sync_ok=true;
  assert(r.controller.apply_preset(2));
  Rig retry; assert(retry.controller.apply_preset(2) && retry.display.overlays.back()=="retry");
  // Exact v1 records migrate without touching output or rewriting rollback data.
  preferences=Preferences{};
  LegacyFavoriteStore legacy{}; legacy.selected=7; legacy.favorites[7].used=1;
  legacy.favorites[7].volume=0.75f;
  std::strcpy(legacy.favorites[7].uri,"https://example.org/old.mp3");
  auto old=preferences.make_preference<LegacyFavoriteStore>(LEGACY_STORAGE_KEY);
  assert(old.save(&legacy) && preferences.sync());
  Rig migrated;
  assert(migrated.controller.current_favorite()==7 && migrated.player.queue.empty());
  assert(migrated.controller.play(7)); migrated.drain();
  assert(migrated.player.volume==0.75f && migrated.player.uri==legacy.favorites[7].uri);
  LegacyFavoriteStore rollback{}; assert(old.load(&rollback) && rollback.selected==7);
  Rig second_boot; assert(second_boot.controller.current_favorite()==7);
  // Malformed v2 is rejected; without valid legacy there are no playable records.
  preferences=Preferences{};
  FavoriteStore corrupt{}; corrupt.selected=9; corrupt.favorites[9].used=1;
  corrupt.favorites[9].snapshot=1; std::memset(corrupt.favorites[9].name,'x',PRESET_NAME_SIZE);
  auto current=preferences.make_preference<FavoriteStore>(STORAGE_KEY);
  assert(current.save(&corrupt) && preferences.sync());
  Rig invalid; assert(invalid.controller.current_favorite()==-1 && !invalid.controller.apply_preset(9));
}
