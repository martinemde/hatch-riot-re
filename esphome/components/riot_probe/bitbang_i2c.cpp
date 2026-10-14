#include "bitbang_i2c.h"

#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include "esphome/core/hal.h"

namespace esphome {
namespace riot_probe {

// Longest a slave may hold SCL low (clock stretching) before we call it a bus error.
static const uint32_t STRETCH_TIMEOUT_US = 2000;

void BitBangI2C::init(uint8_t sda, uint8_t scl, uint32_t frequency) {
  this->sda_pin_ = sda;
  this->scl_pin_ = scl;
  this->half_period_us_ = frequency > 0 ? (500000 + frequency - 1) / frequency : 10;
  for (gpio_num_t pin : {(gpio_num_t) sda, (gpio_num_t) scl}) {
    gpio_reset_pin(pin);
    gpio_set_level(pin, 1);  // released
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    // Stock leaves the internal pull-ups off; the board has external ones.
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
}

bool BitBangI2C::sda_high() const { return gpio_get_level((gpio_num_t) this->sda_pin_); }
bool BitBangI2C::scl_high() const { return gpio_get_level((gpio_num_t) this->scl_pin_); }

void BitBangI2C::delay_() const { esp_rom_delay_us(this->half_period_us_); }

void BitBangI2C::sda_(bool high) { gpio_set_level((gpio_num_t) this->sda_pin_, high ? 1 : 0); }

bool BitBangI2C::scl_(bool high) {
  gpio_set_level((gpio_num_t) this->scl_pin_, high ? 1 : 0);
  if (!high)
    return true;
  // Wait for SCL to actually rise (slow edge or clock stretching).
  uint32_t start = micros();
  while (!this->scl_high()) {
    if (micros() - start > STRETCH_TIMEOUT_US)
      return false;
  }
  return true;
}

bool BitBangI2C::clear() {
  this->sda_(true);
  this->scl_(true);
  this->delay_();
  for (int i = 0; i < 9 && !this->sda_high(); i++) {
    this->scl_(false);
    this->delay_();
    this->scl_(true);
    this->delay_();
  }
  this->stop_();
  return this->sda_high() && this->scl_high();
}

void BitBangI2C::release() {
  this->sda_(true);
  this->scl_(true);
}

bool BitBangI2C::start_() {
  // Bus must be idle: both lines released and high. Also used for repeated START (SCL low, SDA released).
  this->sda_(true);
  this->delay_();
  if (!this->scl_(true) || !this->sda_high())
    return false;
  this->delay_();
  this->sda_(false);
  this->delay_();
  this->scl_(false);
  this->delay_();
  return true;
}

bool BitBangI2C::stop_() {
  this->sda_(false);
  this->delay_();
  bool ok = this->scl_(true);
  this->delay_();
  this->sda_(true);
  this->delay_();
  return ok && this->sda_high();
}

bool BitBangI2C::write_byte_(uint8_t byte, bool *acked) {
  for (int i = 7; i >= 0; i--) {
    this->sda_((byte >> i) & 1);
    this->delay_();
    if (!this->scl_(true))
      return false;
    this->delay_();
    this->scl_(false);
  }
  // ACK bit: release SDA, slave pulls it low.
  this->sda_(true);
  this->delay_();
  if (!this->scl_(true))
    return false;
  this->delay_();
  *acked = !this->sda_high();
  this->scl_(false);
  this->delay_();
  return true;
}

bool BitBangI2C::read_byte_(uint8_t *byte, bool ack) {
  uint8_t value = 0;
  this->sda_(true);
  for (int i = 0; i < 8; i++) {
    this->delay_();
    if (!this->scl_(true))
      return false;
    this->delay_();
    value = (value << 1) | (this->sda_high() ? 1 : 0);
    this->scl_(false);
  }
  this->sda_(!ack);
  this->delay_();
  if (!this->scl_(true))
    return false;
  this->delay_();
  this->scl_(false);
  this->sda_(true);
  *byte = value;
  return true;
}

i2c::ErrorCode BitBangI2C::fail_(i2c::ErrorCode err) {
  // Leave the bus idle for the next attempt.
  this->clear();
  return err;
}

i2c::ErrorCode BitBangI2C::write_regs(uint8_t address, uint8_t reg, const uint8_t *data, size_t len) {
  if (!this->start_())
    return this->fail_(i2c::ERROR_TIMEOUT);
  bool acked = false;
  if (!this->write_byte_(address << 1, &acked))
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!acked)
    return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  if (!this->write_byte_(reg, &acked))
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!acked)
    return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  for (size_t i = 0; i < len; i++) {
    if (!this->write_byte_(data[i], &acked))
      return this->fail_(i2c::ERROR_TIMEOUT);
    if (!acked)
      return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  }
  return this->stop_() ? i2c::ERROR_OK : this->fail_(i2c::ERROR_UNKNOWN);
}

i2c::ErrorCode BitBangI2C::read_regs(uint8_t address, uint8_t reg, uint8_t *data, size_t len) {
  if (!this->start_())
    return this->fail_(i2c::ERROR_TIMEOUT);
  bool acked = false;
  if (!this->write_byte_(address << 1, &acked))
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!acked)
    return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  if (!this->write_byte_(reg, &acked))
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!acked)
    return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  // Repeated START, then read.
  if (!this->start_())
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!this->write_byte_((address << 1) | 1, &acked))
    return this->fail_(i2c::ERROR_TIMEOUT);
  if (!acked)
    return this->fail_(i2c::ERROR_NOT_ACKNOWLEDGED);
  for (size_t i = 0; i < len; i++) {
    if (!this->read_byte_(&data[i], i + 1 < len))
      return this->fail_(i2c::ERROR_TIMEOUT);
  }
  return this->stop_() ? i2c::ERROR_OK : this->fail_(i2c::ERROR_UNKNOWN);
}

bool BitBangI2C::probe(uint8_t address) {
  if (!this->start_()) {
    this->clear();
    return false;
  }
  bool acked = false;
  bool ok = this->write_byte_(address << 1, &acked);
  this->stop_();
  if (!ok)
    this->clear();
  return ok && acked;
}

}  // namespace riot_probe
}  // namespace esphome
