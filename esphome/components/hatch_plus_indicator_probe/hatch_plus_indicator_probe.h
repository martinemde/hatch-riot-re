#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c_bus.h"

namespace esphome::hatch_plus_indicator_probe {
// ACK/address observations are not GreenPAK identity or write authorization.
class HatchPlusIndicatorProbe : public PollingComponent {
 public:
  void set_i2c_bus(i2c::I2CBus *bus) { bus_ = bus; }
  void update() override;
  void dump_config() override;
  void on_shutdown() override { stopped_ = true; valid_ = status_valid_ = false; }
  bool valid() const { return valid_; }
  bool status_valid() const { return status_valid_; }
  bool programmed_address_ack() const { return programmed_ack_; }
  bool blank_address_ack() const { return blank_ack_; }
  bool register_byte(unsigned index, uint8_t &value) const {
    if (!valid_ || index >= 4) return false;
    value = registers_[index];
    return true;
  }
  bool status_byte(unsigned index, uint8_t &value) const {
    if (!status_valid_ || index >= 3) return false;
    value = status_[index];
    return true;
  }
 protected:
  i2c::I2CBus *bus_{nullptr};
  uint8_t registers_[4]{}, status_[3]{};
  bool status_valid_{false};
  bool valid_{false}, stopped_{false}, programmed_ack_{false}, blank_ack_{false};
};
}  // namespace esphome::hatch_plus_indicator_probe
