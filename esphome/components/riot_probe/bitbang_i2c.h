#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace riot_probe {

// Same codes as esphome::i2c::ErrorCode for the ones used here, so the stats keep their meaning.
namespace i2c {
enum ErrorCode : uint8_t {
  ERROR_OK = 0,
  ERROR_NOT_ACKNOWLEDGED = 2,
  ERROR_TIMEOUT = 3,  // SCL held low past the clock-stretch timeout, or the bus was busy at START
  ERROR_UNKNOWN = 6,
};
}  // namespace i2c

// Bit-banged I2C master on two open-drain GPIOs (external pull-ups), ported from hatch_expander. The probe used
// ESP-IDF's I2C master driver; with PSRAM on, its first expander write after a power cycle crashed the unit and left
// it boot-looping until unplugged (2026-10-14). hatch_expander's bit-banged bus runs fine on the same unit.
// Main loop only: not thread-safe.
class BitBangI2C {
 public:
  void init(uint8_t sda, uint8_t scl, uint32_t frequency);
  // Clock out a slave stuck mid-byte (at most 9 bits), then STOP. True if both lines end high.
  bool clear();
  // Release both lines (before a restart, so no slave is left mid-transaction).
  void release();
  bool sda_high() const;
  bool scl_high() const;

  i2c::ErrorCode write_regs(uint8_t address, uint8_t reg, const uint8_t *data, size_t len);
  i2c::ErrorCode read_regs(uint8_t address, uint8_t reg, uint8_t *data, size_t len);
  // Address-only transaction; true if something ACKs.
  bool probe(uint8_t address);

  uint32_t half_period_us() const { return this->half_period_us_; }

 protected:
  void delay_() const;
  void sda_(bool high);
  bool scl_(bool high);  // releasing waits for clock stretching (with a timeout)
  bool start_();
  bool stop_();
  bool write_byte_(uint8_t byte, bool *acked);  // false if SCL got stuck
  bool read_byte_(uint8_t *byte, bool ack);
  i2c::ErrorCode fail_(i2c::ErrorCode err);

  uint8_t sda_pin_{0};
  uint8_t scl_pin_{22};
  uint32_t half_period_us_{10};
};

}  // namespace riot_probe
}  // namespace esphome
