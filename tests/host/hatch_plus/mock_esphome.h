#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace esphome {
inline uint32_t clock_ms = 0;
inline uint32_t millis() { return clock_ms; }
class Component { public: virtual void setup() {} virtual void loop() {} virtual void dump_config() {} };
template<typename... Args> class Trigger {
 public:
  std::vector<std::function<void(Args...)>> callbacks;
  void trigger(Args... args) { for (auto &cb : callbacks) cb(args...); }
};
struct Preferences {
  bool save_ok{true}, sync_ok{true}, exists{false};
  int saves{0}, syncs{0};
  std::vector<uint8_t> pending, disk;
  template<typename T> auto make_preference(uint32_t);
  bool sync() { ++syncs; if (!sync_ok) return false; disk = pending; exists = true; return true; }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
class ESPPreferenceObject {
 public:
  template<typename T> bool load(T *value) {
    if (!preferences.exists || preferences.disk.size() != sizeof(T)) return false;
    std::memcpy(value, preferences.disk.data(), sizeof(T)); return true;
  }
  template<typename T> bool save(const T *value) {
    ++preferences.saves;
    if (!preferences.save_ok) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(value);
    preferences.pending.assign(bytes, bytes + sizeof(T)); return true;
  }
};
template<typename T> auto Preferences::make_preference(uint32_t) { return ESPPreferenceObject{}; }
namespace light {
enum class ColorMode { RGB_WHITE };
struct Values {
  bool on{false}; float red{0}, green{0}, blue{0}, white{0}, brightness{1}, color_brightness{1};
  bool is_on() const { return on; }
  float get_red() const { return red; } float get_green() const { return green; }
  float get_blue() const { return blue; } float get_white() const { return white; }
  float get_brightness() const { return brightness; }
  float get_color_brightness() const { return color_brightness; }
};
class LightState;
class Call {
 public:
  explicit Call(LightState *parent);
  Call &set_color_mode_if_supported(ColorMode) { return *this; }
  Call &set_effect(uint32_t) { return *this; }
  Call &set_transition_length(uint32_t ms) { transition_ms = ms; return *this; }
  Call &set_state(bool v) { value.on = v; return *this; }
  Call &set_brightness(float v) { value.brightness = v; return *this; }
  Call &set_rgb(float r, float g, float b) { value.red=r; value.green=g; value.blue=b; return *this; }
  Call &set_white(float v) { value.white = v; return *this; }
  Call &set_color_brightness(float v) { value.color_brightness = v; return *this; }
  void perform();
 private: LightState *parent; Values value; uint32_t transition_ms{1000};
};
class LightState {
 public:
  Values remote_values, current_values; int calls{0};
  void finish_transition() { current_values = remote_values; }
  Call make_call() { return Call(this); }
  Call turn_off() { return make_call().set_state(false); }
  Call toggle() { return make_call().set_state(!remote_values.on); }
};
inline Call::Call(LightState *p) : parent(p), value(p->remote_values) {}
inline void Call::perform() {
  parent->remote_values = value;
  if (transition_ms == 0) parent->current_values = value;
  ++parent->calls;
}
}
namespace media_player {
enum MediaPlayerCommand { MEDIA_PLAYER_COMMAND_REPEAT_ONE, MEDIA_PLAYER_COMMAND_REPEAT_OFF,
  MEDIA_PLAYER_COMMAND_STOP, MEDIA_PLAYER_COMMAND_VOLUME_UP, MEDIA_PLAYER_COMMAND_VOLUME_DOWN };
struct Request { std::string uri; int command{-1}; };
class MediaPlayer;
class Call {
 public:
  explicit Call(MediaPlayer *p) : parent(p) {}
  Call &set_volume(float v) { volume=v; return *this; }
  Call &set_command(MediaPlayerCommand c) { request.command = c; return *this; }
  Call &set_media_url(const std::string &uri) { request.uri = uri; return *this; }
  void perform();
 private: MediaPlayer *parent; float volume{-1}; Request request;
};
// Deliberately asynchronous: calls queue requests; only tick() accepts playback.
class MediaPlayer {
 public:
  float volume{0.5f}; bool repeat{false}, playing{false}, stopping{false};
  std::string uri; std::vector<Request> queue; int starts{0};
  Call make_call() { return Call(this); }
  void tick() {
    if (queue.empty()) return;
    auto r = queue.front();
    if (!r.uri.empty()) {
      if (playing) { stopping=true; return; }
      uri=r.uri; playing=true; ++starts;
    } else if (r.command == MEDIA_PLAYER_COMMAND_STOP) { playing=false; uri.clear(); }
    else if (r.command == MEDIA_PLAYER_COMMAND_REPEAT_ONE) repeat=true;
    else if (r.command == MEDIA_PLAYER_COMMAND_REPEAT_OFF) repeat=false;
    queue.erase(queue.begin());
  }
  void finish_stop() { playing=false; stopping=false; }
};
inline void Call::perform() {
  if (volume >= 0) parent->volume=volume;
  else if (request.command == MEDIA_PLAYER_COMMAND_VOLUME_UP) parent->volume=std::min(1.0f,parent->volume+0.05f);
  else if (request.command == MEDIA_PLAYER_COMMAND_VOLUME_DOWN) parent->volume=std::max(0.0f,parent->volume-0.05f);
  else parent->queue.push_back(request);
}
}
}
#define ESP_LOGCONFIG(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
