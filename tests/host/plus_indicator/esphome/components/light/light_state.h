#pragma once
#include <cstdint>
#include <string>
namespace esphome::light {
class LightColorValues {
 public:
  float state = 1, brightness = 1, color_brightness = 1, red = 0, green = 0, blue = 0;
  bool is_on() const { return state != 0; }
  float get_brightness() const { return brightness; }
  float get_color_brightness() const { return color_brightness; }
  float get_red() const { return red; }
  float get_green() const { return green; }
  float get_blue() const { return blue; }
};
class LightState;
class LightCall {
 public:
  explicit LightCall(LightState *state) : state_(state) {}
  LightCall &set_rgb(float r, float g, float b);
  LightCall &set_brightness(float value);
  LightCall &set_color_brightness(float value);
  LightCall &set_state(bool value);
  LightCall &set_transition_length(uint32_t value);
  LightCall &set_save(bool value);
  LightCall &set_effect(const char *value);
  void perform();
 private:
  LightState *state_;
};
class LightState {
 public:
  LightColorValues current_values;
  LightColorValues remote_values;
  std::string effect = "None";
  unsigned calls = 0;
  unsigned publications = 0;
  uint32_t transition = 0;
  bool save = false;
  LightCall make_call() { return LightCall(this); }
};
inline LightCall &LightCall::set_rgb(float r, float g, float b) {
  state_->current_values.red = r; state_->current_values.green = g; state_->current_values.blue = b; return *this;
}
inline LightCall &LightCall::set_brightness(float v) { state_->current_values.brightness = v; return *this; }
inline LightCall &LightCall::set_color_brightness(float v) { state_->current_values.color_brightness = v; return *this; }
inline LightCall &LightCall::set_state(bool v) { state_->current_values.state = v; return *this; }
inline LightCall &LightCall::set_transition_length(uint32_t v) { state_->transition = v; return *this; }
inline LightCall &LightCall::set_save(bool v) { state_->save = v; return *this; }
inline LightCall &LightCall::set_effect(const char *v) { state_->effect = v; return *this; }
inline void LightCall::perform() {
  ++state_->calls;
  ++state_->publications;
  state_->remote_values = state_->current_values;
}
}
