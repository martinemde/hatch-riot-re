#include "hatch_accelerometer.h"
#include "esphome/core/log.h"

namespace esphome::hatch_accelerometer {
static const char *const TAG = "hatch_accelerometer";
void HatchAccelerometer::update() {
  if (stopped_ || !expander_) return;
  if (allow_initialization_) {
    if (!initialized_) {
      uint8_t id;
      if (!expander_->read_device_registers(0x27, 0x01, &id, 1) || id != 0x13) {
        invalidate_();
        ESP_LOGW(TAG, "DA213 ID unavailable or unsupported; no initialization writes");
        return;
      }
      // Exact stock645 DA213 reset and contiguous RANGE/ODR_AXIS/MODE_BW write.
      static const uint8_t config[3]={0x00,0x03,0x06};
      if (!expander_->write_device_register(0x27,0x00,0x24) ||
          !expander_->write_device_registers(0x27,0x0f,config,3)) {
        invalidate_();
        return;
      }
      initialized_=true;
      return;  // First sample on next poll, not before the sensor can settle.
    }
    uint8_t raw[6];
    if (!expander_->read_device_registers(0x27,0x02,raw,6)) { invalidate_(); return; }
    acceleration_=decode_da213(raw);
    valid_=true;
    for (unsigned i=0;i<3;i++) if (axes_[i]) axes_[i]->publish_state(acceleration_.g(i));
    if (tilt_) tilt_->publish_state(acceleration_.tilt_degrees());
    if (upright_) upright_->publish_state(acceleration_.upright());
    if (not_upright_) not_upright_->publish_state(!acceleration_.upright());
    return;
  }
  // Main-loop shared owner, never a second I2C master on GPIO0/22.
  uint8_t id;
  if (expander_->read_device_registers(0x27, 0x01, &id, 1))
    ESP_LOGI(TAG, "Discovery 0x27 register 0x01: 0x%02X (DA213 candidate ID 0x13)", id);
  else
    ESP_LOGW(TAG, "Discovery 0x27 register 0x01: unavailable");
  const bool at26 = expander_->probe_device(0x26);
  const bool at62 = expander_->probe_device(0x62);
  ESP_LOGI(TAG, "Discovery ACK: 0x26=%s 0x62=%s", at26 ? "yes" : "no", at62 ? "yes" : "no");
  if (at62) {
    if (expander_->read_device_registers(0x62, 0x11, &id, 1))
      ESP_LOGI(TAG, "Discovery 0x62 register 0x11: 0x%02X (stock 0xDE selects MSA321)", id);
    else
      ESP_LOGW(TAG, "Discovery 0x62 register 0x11: unavailable");
  }
}
void HatchAccelerometer::invalidate_() {
  valid_=false;
  for (auto *axis:axes_) if (axis) axis->publish_state(NAN);
  if (tilt_) tilt_->publish_state(NAN);
  if (upright_) upright_->invalidate_state();
  if (not_upright_) not_upright_->invalidate_state();
}
void HatchAccelerometer::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch DA213 via shared expander bus; initialization=%s", allow_initialization_ ? "enabled" : "disabled (read-only discovery)");
}
}  // namespace esphome::hatch_accelerometer
