#include "hatch_plus_indicator.h"

#include "esphome/components/hatch_plus_power/hatch_plus_power.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::hatch_plus_indicator {

static constexpr uint32_t MIN_WRITE_INTERVAL_MS = 500;
static constexpr uint32_t REFRESH_INTERVAL_MS = 5000;

// Color in the state numbering of HatchPlusPower::set_indicator().
static uint8_t power_state(Color color) {
  static constexpr uint8_t states[] = {0, 2, 3, 1};
  return states[static_cast<uint8_t>(color)];
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
  applied_valid_ = power_->set_indicator(indicator_, power_state(desired_));
  if (applied_valid_) {
    applied_ = desired_;
    last_success_ = now;
  }
}

void HatchPlusIndicator::dump_config() {
  ESP_LOGCONFIG("hatch_plus_indicator", "Rest+ indicator %u: off/red/yellow/green", indicator_);
}

}  // namespace esphome::hatch_plus_indicator
