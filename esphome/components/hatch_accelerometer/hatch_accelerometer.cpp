#include "hatch_accelerometer.h"

#include "esphome/core/log.h"

namespace esphome::hatch_accelerometer {

static const char *const TAG = "hatch_accelerometer";
static constexpr uint8_t DA213_ADDRESS = 0x27;
static constexpr uint8_t MSA311_ADDRESS = 0x62;
static constexpr uint8_t CHIP_ID = 0x13;
static constexpr uint8_t REG_ID = 0x01;
static constexpr uint8_t REG_DATA = 0x02;
static constexpr uint8_t REG_RESET = 0x00;
static constexpr uint8_t REG_RANGE = 0x0F;

void HatchAccelerometer::update() {
  if (this->stopped_ || this->expander_ == nullptr)
    return;
  const bool msa311 = this->model_ == AccelerometerModel::MSA311;
  const uint8_t address = msa311 ? MSA311_ADDRESS : DA213_ADDRESS;
  const char *model_name = msa311 ? "MSA311" : "DA213";
  if (!this->setup_complete_) {
    uint8_t id;
    if (!this->expander_->read_device_registers(address, REG_ID, &id, 1) || id != CHIP_ID) {
      ESP_LOGV(TAG, "%s ID unavailable or unsupported", model_name);
      return;
    }
    if (msa311) {
      uint8_t mode;
      // Stock 7.1.645 uses 0xDE here to distinguish the unsupported MSA321.
      if (!this->expander_->read_device_registers(address, 0x11, &mode, 1) || mode == 0xDE)
        return;
    }
    if (this->allow_initialization_) {
      static constexpr uint8_t reset = 0x24;
      if (!this->expander_->write_device_registers(address, REG_RESET, &reset, 1))
        return;
      if (msa311) {
        // Stock 7.1.503 at 0x62 uses separate ODR/axis and power/bandwidth writes.
        static constexpr uint8_t odr = 0x2C, mode = 0x1E;
        if (!this->expander_->write_device_registers(address, 0x10, &odr, 1) ||
            !this->expander_->write_device_registers(address, 0x11, &mode, 1))
          return;
      } else {
        static constexpr uint8_t config[] = {0x00, 0x03, 0x06};
        if (!this->expander_->write_device_registers(address, REG_RANGE, config, sizeof(config)))
          return;
      }
    }
    this->setup_complete_ = true;
    ESP_LOGI(TAG, "%s detected at 0x%02X (ID 0x%02X)", model_name, address, id);
    return;  // On initialization, wait until the next poll for the first sample.
  }
  if (!this->allow_initialization_)
    return;

  uint8_t raw[6];
  if (!this->expander_->read_device_registers(address, REG_DATA, raw, sizeof(raw))) {
    this->invalidate_();
    return;
  }
  this->acceleration_ = decode_acceleration(raw, msa311);
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
  ESP_LOGCONFIG(TAG, "Hatch %s via shared expander bus; initialization=%s",
                this->model_ == AccelerometerModel::MSA311 ? "MSA311" : "DA213",
                this->allow_initialization_ ? "enabled" : "disabled (read-only discovery)");
}

}  // namespace esphome::hatch_accelerometer
