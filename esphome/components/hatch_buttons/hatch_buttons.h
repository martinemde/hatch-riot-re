#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace esphome::hatch_buttons {
struct Button {
  std::string name;
  bool pressed{false}, held{false}, cancelled{false};
  uint32_t pressed_at{0}, released_at{0};
  uint8_t clicks{0};
};
class HatchButtons : public Component {
 public:
  void loop() override;
  void set_hold_ms(uint32_t ms) { hold_ms_ = ms; }
  void set_multi_click_ms(uint32_t ms) { multi_click_ms_ = ms; }
  void set_allow_when_locked(bool allow) { allow_when_locked_ = allow; }
  void set_child_lock(bool locked);
  bool get_child_lock() const { return child_lock_; }
  void add_button(const std::string &name) { buttons_.push_back({name}); }
  void button_event(const std::string &name, bool pressed);
  void add_on_button_callback(std::function<void(std::string, std::string)> cb) { button_callbacks_.push_back(cb); }
  void add_on_press_callback(std::function<void(std::string)> cb) { press_callbacks_.push_back(cb); }
  void add_on_release_callback(std::function<void(std::string, uint32_t)> cb) { release_callbacks_.push_back(cb); }
  void add_on_cancel_callback(std::function<void(std::string)> cb) { cancel_callbacks_.push_back(cb); }
 protected:
  bool locked_() const { return child_lock_ && !allow_when_locked_; }
  void finish_clicks_(Button &button);
  void hold_(Button &button);
  void emit_(const Button &button, const std::string &gesture);
  std::vector<Button> buttons_;
  std::vector<std::function<void(std::string, std::string)>> button_callbacks_;
  std::vector<std::function<void(std::string)>> press_callbacks_, cancel_callbacks_;
  std::vector<std::function<void(std::string, uint32_t)>> release_callbacks_;
  uint32_t hold_ms_{800}, multi_click_ms_{350};
  bool child_lock_{false}, allow_when_locked_{false};
};
class ButtonTrigger : public Trigger<std::string, std::string> {
 public:
  explicit ButtonTrigger(HatchButtons *parent) {
    parent->add_on_button_callback([this](std::string button, std::string gesture) { trigger(button, gesture); });
  }
};
class PressTrigger : public Trigger<std::string> {
 public:
  explicit PressTrigger(HatchButtons *parent) {
    parent->add_on_press_callback([this](std::string button) { trigger(button); });
  }
};
class ReleaseTrigger : public Trigger<std::string, uint32_t> {
 public:
  explicit ReleaseTrigger(HatchButtons *parent) {
    parent->add_on_release_callback([this](std::string button, uint32_t duration) { trigger(button, duration); });
  }
};
class CancelTrigger : public Trigger<std::string> {
 public:
  explicit CancelTrigger(HatchButtons *parent) {
    parent->add_on_cancel_callback([this](std::string button) { trigger(button); });
  }
};
}  // namespace esphome::hatch_buttons
