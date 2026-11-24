#include "hatch_plus_indicator.h"

#include "esphome/components/hatch_plus_power/hatch_plus_power.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::hatch_plus_indicator {

static constexpr uint32_t MIN_WRITE_INTERVAL_MS = 500;
static constexpr uint32_t REFRESH_INTERVAL_MS = 5000;

light::LightTraits HatchPlusIndicator::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void HatchPlusIndicator::write_state(light::LightState *state) {
  const auto &values = state->current_values;
  // The bicolor LED has no blue channel or dimming.
  if (!values.is_on() || values.get_brightness() <= 0 || values.get_color_brightness() <= 0) {
    desired_ = Color::OFF;
    return;
  }
  const bool red = values.get_red() > 0;
  const bool green = values.get_green() > 0;
  desired_ = red ? (green ? Color::YELLOW : Color::RED) : (green ? Color::GREEN : Color::OFF);
}

void HatchPlusIndicator::loop() {
  if (power_ == nullptr || !power_->indicators_ready()) {
    applied_valid_ = false;
    return;
  }
  const uint32_t now = millis();
  // Reassert after an owner interruption or a GreenPAK reset.
  if (applied_valid_ && desired_ == applied_ && now - last_success_ < REFRESH_INTERVAL_MS)
    return;
  if (attempted_ && now - last_attempt_ < MIN_WRITE_INTERVAL_MS)
    return;
  attempted_ = true;
  last_attempt_ = now;
  applied_valid_ = power_->set_indicator(indicator_, static_cast<uint8_t>(desired_));
  if (applied_valid_) {
    applied_ = desired_;
    last_success_ = now;
  }
}

void HatchPlusIndicator::dump_config() {
  ESP_LOGCONFIG("hatch_plus_indicator", "Rest+ indicator %u: discrete red/green/yellow", indicator_);
}

}  // namespace esphome::hatch_plus_indicator
