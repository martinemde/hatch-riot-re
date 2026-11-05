#include "hatch_plus_indicator.h"

#include <cmath>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome::hatch_plus_indicator {

light::LightTraits HatchPlusIndicator::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void HatchPlusIndicator::write_state(light::LightState *state) {
  const auto &values = state->current_values;
  // Hardware has only discrete red/green channels. Blue is ignored and any
  // positive brightness selects full output; zero brightness still means off.
  if (!values.is_on() || values.get_brightness() <= 0 || values.get_color_brightness() <= 0) {
    desired_ = 0;
    return;
  }
  const bool red = values.get_red() > 0;
  const bool green = values.get_green() > 0;
  desired_ = red ? (green ? 3 : 2) : (green ? 1 : 0);
}

void HatchPlusIndicator::loop() {
  // LightState finishes setup (including restoring its values) before the first
  // component loop. Start the boot effect only when that resolved state is on.
  if (start_automatic_ && state_ != nullptr) {
    start_automatic_ = false;
    if (state_->remote_values.is_on())
      state_->make_call().set_effect("Automatic").set_save(false).perform();
  }
  if (power_ == nullptr || !power_->indicators_ready()) {
    applied_valid_ = false;
    return;
  }
  const uint32_t now = millis();
  // Charging blink is a hardware phase, not a change to the light's logical
  // color/brightness. Manual colors (effect None) must remain steady.
  uint8_t desired = automatic_active_ ? automatic_desired_ : desired_;
  if (automatic_active_ &&
      ((state_ != nullptr && !state_->remote_values.is_on()) ||
       (indicator_ == BATTERY && power_->indicator_charging() && (now / 500) % 2 != 0)))
    desired = 0;
  if (applied_valid_ && desired == applied_ && now - last_success_ < 5000)
    return;
  if (attempted_ && now - last_attempt_ < 500)
    return;
  attempted_ = true;
  last_attempt_ = now;
  if (power_->set_indicator(indicator_, desired)) {
    applied_ = desired;
    applied_valid_ = true;
    last_success_ = now;
  } else {
    applied_valid_ = false;
  }
}

uint8_t HatchPlusIndicator::automatic_color() const {
  switch (indicator_) {
    case WIFI:
      return wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected() ? 1 : 2;
    case BATTERY: {
      if (power_ == nullptr)
        return 0;
      const float percent = power_->indicator_battery_percent();
      if (!std::isfinite(percent) || percent < 0 || percent > 100)
        return 0;
      return percent < 25 ? 2 : (percent < 50 ? 3 : 1);
    }
    case LOCK:
      return hatch_ != nullptr && hatch_->get_child_lock() ? 2 : 0;
    default:
      return 0;
  }
}

void AutomaticEffect::apply() {
  const uint32_t now = millis();
  if (!first_ && now - last_update_ < 500)
    return;
  first_ = false;
  last_update_ = now;
  // Automatic status is physical output only. HA owns the logical enable,
  // manual color and brightness; none of those values track firmware status.
  output_->update_automatic_color();
}

void HatchPlusIndicator::dump_config() {
  ESP_LOGCONFIG("hatch_plus_indicator", "Plus indicator %u: discrete red/green/yellow; shared power owner", indicator_);
}

}  // namespace esphome::hatch_plus_indicator
