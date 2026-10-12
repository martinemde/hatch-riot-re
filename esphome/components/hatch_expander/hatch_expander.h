#pragma once

#include "esphome/components/gpio_expander/cached_gpio.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::hatch_expander {

class HatchExpander final : public Component, public gpio_expander::CachedGpioExpander<uint16_t, 16> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Stop all bus traffic before an OTA/restart reboot and leave both lines released.
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_bus(uint8_t sda, uint8_t scl, uint8_t address, uint32_t frequency) {
    this->sda_ = sda;
    this->scl_ = scl;
    this->address_ = address;
    this->half_period_us_ = frequency > 0 ? (500000 + frequency - 1) / frequency : 10;
  }
  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_p0_push_pull(bool on) { this->p0_push_pull_ = on; }

  void pin_mode(uint8_t pin, gpio::Flags flags);

 protected:
  static void IRAM_ATTR gpio_intr(HatchExpander *arg);

  bool digital_read_hw(uint8_t pin) override;
  bool digital_read_cache(uint8_t pin) override { return this->inputs_ & (1u << pin); }
  void digital_write_hw(uint8_t pin, bool value) override;

  // Read-modify-write one register byte: only `mask` bits change, so pins outside the stock model keep their
  // power-on state.
  bool try_init_();
  void apply_output_(uint8_t pin, bool value);
  void apply_mode_(uint8_t pin, bool input);
  bool update_reg_(uint8_t reg, uint8_t mask, uint8_t bits, bool verify = true);
  bool write_reg_(uint8_t reg, uint8_t value);
  bool read_regs_(uint8_t reg, uint8_t *data, size_t len);
  void settle_();
  // Extend the no-transaction window by gap_ms from now.
  void mark_txn_(uint32_t gap_ms);

  // --- bit-banged I2C master (open-drain: 0 = drive low, 1 = release; external pull-ups) ---
  void bb_init_();
  bool bb_clear_();  // 9 clocks until SDA is released, then STOP; true if both lines end high
  void bb_delay_() const;
  void bb_sda_(bool high);
  bool bb_scl_(bool high);  // releasing waits for clock stretching (with a timeout)
  bool bb_start_();
  bool bb_stop_();
  bool bb_write_byte_(uint8_t byte);  // true on ACK
  bool bb_read_byte_(uint8_t *byte, bool ack);
  bool bb_write_reg_(uint8_t reg, uint8_t value);
  bool bb_read_regs_(uint8_t reg, uint8_t *data, size_t len);
  void bb_fail_(const char *what);

  uint8_t sda_{0};
  uint8_t scl_{22};
  uint8_t address_{0x58};
  uint32_t half_period_us_{10};
  uint32_t bus_errors_{0};

  InternalGPIOPin *interrupt_pin_{nullptr};
  bool p0_push_pull_{true};
  uint16_t inputs_{0xFFFF};  // all released (buttons are active-low) until a trusted read
  uint8_t out_[2]{0, 0};
  uint8_t cfg_[2]{0, 0};
  // What the pins asked for, replayed if the chip only answers after their setup().
  uint16_t mode_set_{0};
  uint16_t mode_input_{0};
  uint16_t out_set_{0};
  uint16_t out_desired_{0};
  uint32_t init_attempts_{0};
  uint32_t quiet_until_{0};
  uint32_t last_read_{0};
  uint32_t read_failures_{0};
  bool ready_{false};
};

class HatchExpanderPin final : public GPIOPin, public Parented<HatchExpander> {
 public:
  void setup() override { this->pin_mode(this->flags_); }
  void pin_mode(gpio::Flags flags) override { this->parent_->pin_mode(this->pin_, flags); }
  bool digital_read() override { return this->parent_->digital_read(this->pin_) != this->inverted_; }
  void digital_write(bool value) override { this->parent_->digital_write(this->pin_, value != this->inverted_); }
  size_t dump_summary(char *buffer, size_t len) const override;
  gpio::Flags get_flags() const override { return this->flags_; }

  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_inverted(bool inverted) { this->inverted_ = inverted; }
  void set_flags(gpio::Flags flags) { this->flags_ = flags; }

 protected:
  uint8_t pin_{0};
  bool inverted_{false};
  gpio::Flags flags_{gpio::FLAG_NONE};
};

}  // namespace esphome::hatch_expander
