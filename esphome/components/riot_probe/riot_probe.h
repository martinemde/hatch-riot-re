#pragma once

#include <string>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome {
namespace riot_probe {

// 7-segment glyph for an ASCII character (stock font table at 0x3f486910, bit7 = DP).
uint8_t seg_encode(char c);
// Brightness byte for display level 0..15 (stock table at 0x3f432e0c).
uint8_t seg_brightness(uint8_t level);

class RiotProbe : public Component, public i2c::I2CDevice {
 public:
  // Run before the I2C buses are set up so that variant detection sees the raw pins, as the stock
  // firmware does.
  float get_setup_priority() const override { return setup_priority::BUS + 10.0f; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  void set_write_timeout(uint32_t ms) { this->write_timeout_ = ms; }

  // --- read-only API ---
  const std::string &variant() const { return this->variant_; }
  uint8_t chip_id() const { return this->chip_id_; }
  bool expander_ok() const { return this->snapshot_ok_; }
  uint16_t inputs() const { return this->inputs_; }
  // Input bits seen changing since boot. These pins are treated as inputs and never driven.
  uint16_t seen_toggling() const { return this->seen_toggling_; }
  uint32_t change_count() const { return this->change_count_; }
  uint16_t driven_mask() const { return this->driven_mask_; }
  bool armed() const { return this->armed_; }
  bool p0_push_pull_modified() const { return this->gcr_modified_; }
  // Log `count` registers from `start` of the device at `address` (same bus). Read only.
  void dump(uint8_t address, uint8_t start, uint8_t count);
  void dump_expander();

  // --- guarded write API (expander @0x58 only) ---
  void arm(bool armed);
  // Drive one expander pin (0..15, P0_0..P1_7) to `high`. Reverts after write_timeout.
  bool drive_pin(uint8_t pin, bool high);
  // Set AW9523B P0 to push-pull (GCR bit 4), as the stock driver does. Reverted with everything else.
  bool set_p0_push_pull(bool push_pull);
  // Restore OUT/CFG/GCR registers to the values read at boot.
  void restore();

 protected:
  void detect_variant_();
  bool snapshot_();
  bool read_inputs_(uint16_t *value);
  bool guarded_write_(uint8_t reg, uint8_t value);
  bool read_reg_(uint8_t reg, uint8_t *value);

  uint32_t poll_interval_{50};
  uint32_t write_timeout_{30000};
  uint32_t last_poll_{0};
  uint32_t ready_at_{0};

  std::string variant_{"unknown"};
  uint8_t sense_[3]{0, 0, 0};

  bool snapshot_tried_{false};
  bool snapshot_ok_{false};
  uint8_t chip_id_{0};
  uint8_t snap_out_[2]{0, 0};
  uint8_t snap_cfg_[2]{0xFF, 0xFF};
  uint8_t snap_gcr_{0};

  bool have_inputs_{false};
  uint16_t inputs_{0};
  uint16_t seen_toggling_{0};
  uint32_t change_count_{0};

  bool armed_{false};
  uint16_t driven_mask_{0};
  bool gcr_modified_{false};
};

}  // namespace riot_probe
}  // namespace esphome
