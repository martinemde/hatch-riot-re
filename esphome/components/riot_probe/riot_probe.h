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
  // Set the button pins P0_0-P0_4 to INPUT once at boot, as the stock firmware does, without arming.
  void set_buttons_at_boot(bool on) { this->buttons_at_boot_ = on; }

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
  bool inputs_mode() const { return this->cfg_modified_; }
  // Log `count` registers from `start` of the device at `address` (same bus). Read only.
  void dump(uint8_t address, uint8_t start, uint8_t count);
  void dump_expander();

  // --- guarded write API (expander @0x58 only) ---
  void arm(bool armed);
  // Drive one expander pin (0..15, P0_0..P1_7) to `high`. Reverts after write_timeout. P0_6, P0_7 and
  // P1_4-P1_7 are refused (the stock firmware never changes them).
  bool drive_pin(uint8_t pin, bool high);
  // Set AW9523B P0 to push-pull (GCR bit 4), as the stock driver does. Reverted with everything else.
  bool set_p0_push_pull(bool push_pull);
  // Put the button pins P0_0-P0_4 into INPUT mode (CFG0 |= 0x1F, the stock config) so presses can be read.
  // Auto-reverts to the boot snapshot like any other guarded write. `on=false` reverts immediately.
  bool set_button_inputs(bool on);
  // Restore OUT/CFG/GCR registers to the values read at boot.
  void restore();

 protected:
  void detect_variant_();
  bool snapshot_();
  bool read_inputs_(uint16_t *value);
  bool guarded_write_(uint8_t reg, uint8_t value);
  // Write one expander register, retrying once on failure (the bus is flaky). Logs the I2C error code.
  bool write_reg_(uint8_t reg, uint8_t value);
  bool read_reg_(uint8_t reg, uint8_t *value);
  // Block until the post-write quiet period has passed.
  void settle_();
  // I2C health counters for expander traffic, logged every 30 s.
  void count_(bool write, i2c::ErrorCode err);
  void log_i2c_stats_();

  static constexpr int ERR_SLOTS = 8;
  uint32_t win_reads_{0};
  uint32_t win_writes_{0};
  uint32_t win_errors_[ERR_SLOTS]{};
  uint32_t total_ops_{0};
  uint32_t total_errors_{0};
  bool buttons_at_boot_{false};

  uint32_t poll_interval_{50};
  uint32_t fallback_interval_{1000};  // idle read rate when GPIO34 INT is high
  uint32_t quiet_until_{0};           // no expander transaction before this millis()
  uint32_t write_timeout_{30000};
  uint32_t last_poll_{0};
  uint32_t ready_at_{0};
  uint32_t fail_streak_{0};  // consecutive I2C failures; drives poll back-off

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
  uint16_t candidate_value_{0};    // debounce: value awaiting a confirming second read
  uint8_t candidate_streak_{0};    // consecutive polls the candidate has held

  bool armed_{false};
  uint16_t driven_mask_{0};
  bool gcr_modified_{false};
  bool cfg_modified_{false};
};

}  // namespace riot_probe
}  // namespace esphome
