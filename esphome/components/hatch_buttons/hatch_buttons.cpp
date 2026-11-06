#include "hatch_buttons.h"
#include "esphome/core/hal.h"

namespace esphome::hatch_buttons {
void HatchButtons::set_child_lock(bool locked) {
  child_lock_ = locked;
  if (!locked_()) return;
  for (auto &button : buttons_) {
    const bool active = !button.cancelled && (button.pressed || button.clicks != 0);
    button.clicks = 0;
    button.cancelled = button.pressed;
    if (active) for (auto &callback : cancel_callbacks_) callback(button.name);
  }
}
void HatchButtons::emit_(const Button &button, const std::string &gesture) {
  for (auto &callback : button_callbacks_) {
    if (button.cancelled || locked_()) break;
    callback(button.name, gesture);
  }
}
void HatchButtons::finish_clicks_(Button &button) {
  const uint8_t clicks = button.clicks;
  button.clicks = 0;
  if (clicks != 0) emit_(button, clicks == 1 ? "tap" : clicks == 2 ? "double" : "triple");
}
void HatchButtons::hold_(Button &button) {
  // A preceding short click is a separate gesture, not a double-and-hold.
  finish_clicks_(button);
  if (button.cancelled || locked_()) return;
  button.held = true;
  emit_(button, "hold");
}
void HatchButtons::loop() {
  if (locked_()) return;
  const uint32_t now = millis();
  for (auto &button : buttons_) {
    if (button.cancelled) continue;
    if (button.pressed && !button.held && static_cast<uint32_t>(now - button.pressed_at) >= hold_ms_)
      hold_(button);
    else if (!button.pressed && button.clicks != 0 && static_cast<uint32_t>(now - button.released_at) >= multi_click_ms_)
      finish_clicks_(button);
  }
}
void HatchButtons::button_event(const std::string &name, bool pressed) {
  for (auto &button : buttons_) {
    if (button.name != name || button.pressed == pressed) continue;
    const uint32_t now = millis();
    if (locked_()) {
      button.pressed = pressed;
      button.clicks = 0;
      button.cancelled = pressed;
      return;
    }
    if (pressed) {
      // Latch before callbacks so a synchronous lock cancels this press too.
      button.pressed = true;
      button.pressed_at = now;
      button.held = false;
      if (button.clicks != 0 && static_cast<uint32_t>(now - button.released_at) >= multi_click_ms_)
        finish_clicks_(button);
      if (button.cancelled || locked_()) return;
      for (auto &callback : press_callbacks_) {
        if (button.cancelled || locked_()) break;
        callback(button.name);
      }
      return;
    }
    if (button.cancelled) {
      button.pressed = button.cancelled = false;
      return;
    }
    const uint32_t duration = static_cast<uint32_t>(now - button.pressed_at);
    // A stale loop cannot turn a long hold into a short tap.
    if (!button.held && duration >= hold_ms_) hold_(button);
    if (button.cancelled || locked_()) {
      button.pressed = button.cancelled = false;
      return;
    }
    button.pressed = false;
    button.released_at = now;
    if (!button.held) ++button.clicks;
    for (auto &callback : release_callbacks_) {
      if (locked_()) break;
      callback(button.name, duration);
    }
    if (button.clicks == 3) finish_clicks_(button);
    return;
  }
}
}  // namespace esphome::hatch_buttons
