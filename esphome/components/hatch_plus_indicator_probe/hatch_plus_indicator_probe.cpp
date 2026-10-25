#include "hatch_plus_indicator_probe.h"
#include "esphome/core/log.h"

namespace esphome::hatch_plus_indicator_probe {
static const char *const TAG = "hatch_plus_indicator_probe";

void HatchPlusIndicatorProbe::update() {
  valid_ = status_valid_ = programmed_ack_ = blank_ack_ = false;
  if (stopped_ || bus_ == nullptr) return;
  const auto result = bus_->write_readv(0x10, nullptr, 0, nullptr, 0);
  if (result == i2c::ERROR_NOT_ACKNOWLEDGED) {
    const auto blank_result = bus_->write_readv(0x08, nullptr, 0, nullptr, 0);
    blank_ack_ = blank_result == i2c::ERROR_OK;
    ESP_LOGW(TAG, "0x10 NACK; 0x08 probe result=%u (ACK is not device identity); no register read",
             unsigned(blank_result));
    return;
  }
  if (result != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "0x10 probe failed=%u; no fallback or register read", unsigned(result));
    return;
  }
  programmed_ack_ = true;
  const uint8_t reg = 0x32;
  uint8_t bytes[4];
  const auto read_result = bus_->write_readv(0x10, &reg, 1, bytes, sizeof(bytes));
  if (read_result != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "0x10 ACK; RAM 0x32..0x35 read failed=%u", unsigned(read_result));
  } else {
    for (unsigned i = 0; i < 4; i++) registers_[i] = bytes[i];
    valid_ = true;
    ESP_LOGI(TAG, "0x10 ACK; RAM 32..35: %02X %02X %02X %02X (identity/colors unverified)",
             bytes[0], bytes[1], bytes[2], bytes[3]);
  }
  const uint8_t status_reg = 0x74;
  uint8_t status[3];
  const auto status_result = bus_->write_readv(0x10, &status_reg, 1, status, sizeof(status));
  if (status_result != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "0x10 ACK; RAM 0x74..0x76 read failed=%u", unsigned(status_result));
    return;
  }
  for (unsigned i = 0; i < 3; i++) status_[i] = status[i];
  status_valid_ = true;
  ESP_LOGI(TAG, "0x10 ACK; RAM 74..76: %02X %02X %02X (raw status; identity unverified)",
           status[0], status[1], status[2]);
  ESP_LOGI(TAG, "Voltage flags: wired=%s wireless=%s charge_enable=%s (logic, not charging current)",
           (status[0] & 0x40) ? "on" : "off", (status[0] & 0x20) ? "on" : "off",
           (status[2] & 0x04) ? "on" : "off");
}

void HatchPlusIndicatorProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "Plus bus1 indicator probe: ACK-only discovery, RAM reads only; no indicator/power writes");
}
}  // namespace esphome::hatch_plus_indicator_probe
