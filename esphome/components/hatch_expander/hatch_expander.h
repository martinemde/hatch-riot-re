#pragma once

#include "esphome/components/gpio_expander/cached_gpio.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::hatch_expander {

class HatchExpander final : public Component,
                            public i2c::I2CDevice,
                            public gpio_expander::CachedGpioExpander<uint16_t, 16> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }

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
  bool update_reg_(uint8_t reg, uint8_t mask, uint8_t bits);
  bool write_reg_(uint8_t reg, uint8_t value);
  bool read_regs_(uint8_t reg, uint8_t *data, size_t len);
  void settle_();

  InternalGPIOPin *interrupt_pin_{nullptr};
  bool p0_push_pull_{true};
  uint16_t inputs_{0};
  uint8_t out_[2]{0, 0};
  uint8_t cfg_[2]{0, 0};
  uint16_t input_pins_{0};
  uint32_t quiet_until_{0};
  uint32_t last_read_{0};
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
