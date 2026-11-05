#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome::hatch_plus_power {
// Standalone adapter only. Bus read/write(address7, register, data, length)
// return true on success. Address is supplied by discovery; no address guesses,
// reset sequence, NVM programming or live component wiring lives here.
template<class Bus> class GreenPakRegisterTransport {
 public:
  GreenPakRegisterTransport(Bus &bus, uint8_t address) : bus_(bus), address_(address) {}
  void set_address(uint8_t address) { address_ = address; }
  void set_available(bool available) { available_ = available; }
  int read(uint16_t bit) { return read_bits(bit, 1); }
  int read_pin(uint16_t bit) { return read(bit); }
  int read() { return read(0x3db); }
  int read_bits(uint16_t bit, uint8_t length) {
    if (!available_) return -2;  // Stock part-discovery state2: no bus access.
    if (!length || length > 8) return -1;
    uint8_t bytes[2]{};
    const unsigned offset = bit & 7;
    const size_t count = (offset + length + 7) / 8;
    if (!bus_.read(address_, uint8_t(bit >> 3), bytes, count)) return -1;
    return ((unsigned(bytes[0]) | (unsigned(bytes[1]) << 8)) >> offset) & ((1U << length) - 1);
  }
  bool set(uint8_t mask) { return virtual_bits(mask, true); }
  bool clear(uint8_t mask) { return virtual_bits(mask, false); }
  bool reference(uint8_t value) { return write_bits(0x462, 6, value); }
  bool write_bits(uint16_t bit, uint8_t length, uint8_t value) {
    if (!available_ || !length || length > 8) return false;
    uint8_t bytes[2]{};
    const unsigned offset = bit & 7;
    const size_t count = (offset + length + 7) / 8;
    const uint16_t mask = ((1U << length) - 1) << offset;
    if (!bus_.read(address_, uint8_t(bit >> 3), bytes, count)) return false;
    uint16_t word = unsigned(bytes[0]) | (unsigned(bytes[1]) << 8);
    word = (word & ~mask) | ((uint16_t(value) << offset) & mask);
    bytes[0] = uint8_t(word); bytes[1] = uint8_t(word >> 8);
    return bus_.write(address_, uint8_t(bit >> 3), bytes, count);
  }
 private:
  bool virtual_bits(uint8_t mask, bool set) {
    if (!available_) return false;
    uint8_t value;
    if (!bus_.read(address_, 0x7a, &value, 1)) return false;
    value = set ? value | mask : value & ~mask;
    return bus_.write(address_, 0x7a, &value, 1);
  }
  Bus &bus_;
  uint8_t address_;
  bool available_{true};
};
}  // namespace esphome::hatch_plus_power
