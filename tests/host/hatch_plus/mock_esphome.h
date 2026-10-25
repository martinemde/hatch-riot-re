#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
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
  std::map<uint32_t, std::vector<uint8_t>> pending, disk;
  template<typename T> auto make_preference(uint32_t);
  bool sync() {
    ++syncs;
    if (!sync_ok) { pending.clear(); return false; }
    for (const auto &entry : pending) disk[entry.first] = entry.second;
    pending.clear(); exists = true; return true;
  }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
class ESPPreferenceObject {
 public:
  explicit ESPPreferenceObject(uint32_t key = 0) : key_(key) {}
  template<typename T> bool load(T *value) {
    const auto found = preferences.disk.find(key_);
    if (!preferences.exists || found == preferences.disk.end() || found->second.size() != sizeof(T)) return false;
    std::memcpy(value, found->second.data(), sizeof(T)); return true;
  }
  template<typename T> bool save(const T *value) {
    ++preferences.saves;
    if (!preferences.save_ok) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(value);
    preferences.pending[key_].assign(bytes, bytes + sizeof(T)); return true;
  }
 private:
  uint32_t key_;
};
template<typename T> auto Preferences::make_preference(uint32_t key) { return ESPPreferenceObject{key}; }
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
namespace switch_ {
class Switch {
 public:
  bool state{false};
  std::function<void(bool)> on_control;
  void control(bool value) { state = value; if (on_control) on_control(value); }
  void turn_on() { control(true); }
  void turn_off() { control(false); }
};
}
namespace select {
class Select;
class Call {
 public:
  explicit Call(Select *parent) : parent_(parent) {}
  Call &set_option(const std::string &value) { value_ = value; return *this; }
  void perform();
 private:
  Select *parent_; std::string value_;
};
class Select {
 public:
  std::string state{"None"};
  std::vector<std::string> options{"None", "Rain", "Ocean"};
  std::function<void(const std::string &)> on_control;
  bool has_state() const { return !state.empty(); }
  struct Option { std::string value; std::string str() const { return value; } };
  Option current_option() const { return {state}; }
  bool has_option(const std::string &value) const { return std::find(options.begin(),options.end(),value)!=options.end(); }
  Call make_call() { return Call(this); }
};
inline void Call::perform() { if (parent_->has_option(value_)) { parent_->state=value_; if(parent_->on_control)parent_->on_control(value_); } }
}
namespace text {
class Text;
class Call {
 public:
  explicit Call(Text *parent) : parent_(parent) {}
  Call &set_value(const std::string &value) { value_ = value; return *this; }
  void perform();
 private:
  Text *parent_; std::string value_;
};
class Text {
 public:
  std::string state;
  std::function<void(const std::string &)> on_control;
  Call make_call() { return Call(this); }
};
inline void Call::perform() { parent_->state = value_; if(parent_->on_control)parent_->on_control(value_); }
}
namespace hatch_display {
class HatchDisplay {
 public:
  std::vector<std::string> overlays;
  std::string underlying;
  bool overlay_active{false};
  uint32_t overlay_duration{0};
  void show_scrolling_text_once(const std::string &value) { overlays.push_back(value); overlay_active=true; overlay_duration=2000; }
  void show_text_overlay(const std::string &value, uint32_t duration) { overlays.push_back(value); overlay_active=true; overlay_duration=duration; }
  void clear_overlay() { overlay_active=false; }
};
}

}
#define ESP_LOGCONFIG(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
