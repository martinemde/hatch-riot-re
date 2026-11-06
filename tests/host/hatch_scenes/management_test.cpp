#include "hatch_scenes.h"
#include <cassert>
using namespace esphome;
using namespace esphome::hatch_scenes;
struct Rig {
  light::LightState light, clock;
  media_player::MediaPlayer player;
  select::Select stock;
  switch_::Switch repeat, show_clock;
  text::Text text;
  hatch_display::HatchDisplay display;
  HatchScenes scenes;
  Rig() {
    scenes.set_light(&light); scenes.set_media_player(&player);
    scenes.set_stock_sound(&stock); scenes.set_loop_switch(&repeat);
    scenes.set_clock_switch(&show_clock); scenes.set_clock_light(&clock);
    scenes.set_text(&text); scenes.set_display(&display);
    stock.state="Rain";
    scenes.setup();
  }
  void hold(uint32_t duration, bool tick=true) {
    scenes.management_press(); clock_ms += duration;
    if (tick) scenes.loop();
    scenes.management_release(duration);
  }
};
int main() {
  preferences=Preferences{};
  Rig r;
  r.hold(4999); assert(r.scenes.current_favorite()==-1 && preferences.saves==0);
  r.hold(5000); assert(r.scenes.current_favorite()==0 && r.display.overlays.back()=="SAV0");
  r.hold(9999); assert(r.scenes.current_favorite()==1 && r.display.overlays.back()=="SAV1");
  // Ten seconds previews but does not erase until release; target remains fixed.
  r.scenes.management_press(); clock_ms+=10000; r.scenes.loop();
  assert(r.display.overlays.back()=="DEL1");
  assert(r.scenes.apply_preset(0));
  assert(r.display.overlays.back()=="DEL1");
  r.scenes.management_release(10000);
  assert(!r.scenes.play(1) && r.scenes.play(0));
  // Cancellation (from shared buttons child lock) clears preview and cannot save/delete.
  r.scenes.management_press(); clock_ms+=10000; r.scenes.loop();
  const auto writes=preferences.saves;
  r.scenes.management_cancel(); r.scenes.management_release(10000);
  assert(preferences.saves==writes && r.scenes.play(0));
  // Stalled-loop release still enforces threshold, duplicate release is inert.
  r.hold(10000,false); assert(!r.scenes.play(0));
  const auto after_delete=preferences.saves;
  r.scenes.management_release(10000); assert(preferences.saves==after_delete);
  r.hold(10000); assert(r.display.overlays.back()=="NONE");
  // Timing is unsigned and survives wraparound.
  clock_ms=UINT32_MAX-2500; r.hold(5000);
  assert(r.scenes.current_favorite()==0);
  for (int i=1; i<10; ++i) assert(r.scenes.store_preset(i,""));
  const auto full=preferences.saves;
  r.hold(5000); assert(preferences.saves==full && r.display.overlays.back()=="FULL");
  preferences.save_ok=false;
  r.hold(10000); assert(r.display.overlays.back()=="ERR");
  // No preset entities: raw callbacks have no effect and do not dereference display.
  HatchScenes basic; basic.set_light(&r.light); basic.set_media_player(&r.player);
  basic.setup(); basic.management_press(); clock_ms+=10000; basic.loop(); basic.management_release(10000);
}
