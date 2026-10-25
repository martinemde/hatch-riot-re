#include "hatch_plus.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace esphome;
using namespace esphome::hatch_plus;
int main() {
  preferences = Preferences{};
  light::LightState light;
  media_player::MediaPlayer player;
  HatchPlus controller;
  controller.set_light(&light); controller.set_media_player(&player); controller.setup();
  controller.add_button("night_light", "toggle_light", "none");
  controller.add_button("brightness_up", "brightness_up", "none");
  controller.add_button("brightness_down", "brightness_down", "none");
  auto tap = [&](const char *name) {
    controller.button_event(name,true); ++clock_ms; controller.button_event(name,false);
    clock_ms+=350; controller.loop();
  };
  // Mixed RGB + white is preserved through capture, persisted reload and replay.
  light.make_call().set_state(true).set_rgb(0.2f,0.4f,0.6f).set_white(0.8f)
       .set_brightness(0.5f).set_color_brightness(0.7f).perform();
  assert(controller.configure_favorite(0,"",false));
  assert(controller.capture(0));
  const int writes = preferences.saves;
  tap("brightness_up"); assert(std::abs(light.remote_values.brightness-0.6f)<0.0001f);
  tap("night_light"); assert(!light.remote_values.on);
  tap("brightness_up"); assert(light.remote_values.on);
  for (int i=0;i<20;++i) tap("brightness_up");
  assert(light.remote_values.brightness==1.0f);
  for (int i=0;i<20;++i) tap("brightness_down");
  assert(light.remote_values.brightness==0.0f && !light.remote_values.on);
  assert(preferences.saves==writes && player.queue.empty());
  controller.set_child_lock(true);
  int calls=light.calls; tap("night_light"); tap("brightness_up");
  assert(light.calls==calls);
  // Lock only blocks physical buttons, not direct light/API calls.
  light.make_call().set_state(true).set_brightness(0.25f).perform();
  assert(light.remote_values.on && light.remote_values.brightness==0.25f);
  HatchPlus rebooted;
  rebooted.set_light(&light); rebooted.set_media_player(&player);
  calls=light.calls; rebooted.setup();
  assert(light.calls==calls && player.queue.empty()); // Setup never auto-plays.
  assert(rebooted.play(0));
  const auto &v=light.remote_values;
  assert(v.on && v.red==0.2f && v.green==0.4f && v.blue==0.6f && v.white==0.8f);
  assert(v.brightness==0.5f && v.color_brightness==0.7f);
  while (!player.queue.empty()) player.tick();
  assert(!player.playing && player.uri.empty()); // Empty URI is a light-only favorite.
  std::cout << "light: real controller RGBW capture/reload/buttons passed (light backend mocked)\n";
}
