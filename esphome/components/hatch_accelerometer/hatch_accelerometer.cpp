#include "hatch_accelerometer.h"

#include "esphome/core/log.h"

namespace esphome::hatch_accelerometer {

static const char *const TAG = "hatch_accelerometer";
static constexpr uint8_t ADDRESS = 0x27;
static constexpr uint8_t CHIP_ID = 0x13;
static constexpr uint8_t REG_ID = 0x01;
static constexpr uint8_t REG_DATA = 0x02;
static constexpr uint8_t REG_RESET = 0x00;
static constexpr uint8_t REG_RANGE = 0x0F;

void HatchAccelerometer::update() {
  if (this->stopped_ || this->expander_ == nullptr)
    return;
  if (!this->setup_complete_) {
    uint8_t id;
    if (!this->expander_->read_device_registers(ADDRESS, REG_ID, &id, 1) || id != CHIP_ID) {
      ESP_LOGV(TAG, "DA213 ID unavailable or unsupported");
      return;
    }
    if (this->allow_initialization_) {
      // Stock reset and contiguous RANGE/ODR_AXIS/MODE_BW write (hardware-map.md).
      static constexpr uint8_t reset = 0x24;
      static constexpr uint8_t config[] = {0x00, 0x03, 0x06};
      if (!this->expander_->write_device_registers(ADDRESS, REG_RESET, &reset, 1) ||
          !this->expander_->write_device_registers(ADDRESS, REG_RANGE, config, sizeof(config)))
        return;
    }
    this->setup_complete_ = true;
    ESP_LOGI(TAG, "DA213 detected at 0x%02X (ID 0x%02X)", ADDRESS, id);
    return;  // On initialization, wait until the next poll for the first sample.
  }
  if (!this->allow_initialization_)
    return;

  uint8_t raw[6];
  if (!this->expander_->read_device_registers(ADDRESS, REG_DATA, raw, sizeof(raw))) {
    this->invalidate_();
    return;
  }
  this->acceleration_ = decode_da213(raw);
  this->valid_ = true;
  for (unsigned i = 0; i < 3; i++) {
    if (this->axes_[i] != nullptr)
      this->axes_[i]->publish_state(this->acceleration_.g(i));
  }
  if (this->tilt_ != nullptr)
    this->tilt_->publish_state(this->acceleration_.tilt_degrees());
  if (this->upright_ != nullptr)
    this->upright_->publish_state(this->is_upright());
  if (this->not_upright_ != nullptr)
    this->not_upright_->publish_state(!this->is_upright());
}

void HatchAccelerometer::invalidate_() {
  this->valid_ = false;
  for (auto *axis : this->axes_) {
    if (axis != nullptr)
      axis->publish_state(NAN);
  }
  if (this->tilt_ != nullptr)
    this->tilt_->publish_state(NAN);
  if (this->upright_ != nullptr)
    this->upright_->invalidate_state();
  if (this->not_upright_ != nullptr)
    this->not_upright_->invalidate_state();
}

void HatchAccelerometer::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch DA213 via shared expander bus; initialization=%s",
                this->allow_initialization_ ? "enabled" : "disabled (read-only discovery)");
}

}  // namespace esphome::hatch_accelerometer
