#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::hatch_plus_power {
// Facade over the existing physical bus owner. Native write_readv returns zero
// on success. Register pointer and payload remain in one transaction.
template<class Owner> class NativeI2CBus {
 public:
  explicit NativeI2CBus(Owner &owner) : owner_(owner) {}
  bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    return owner_.write_readv(address, &reg, 1, data, size) == 0;
  }
  bool write(uint8_t address, uint8_t reg, const uint8_t *data, size_t size) {
    if (size > 240) return false;
    std::array<uint8_t, 241> bytes;
    bytes[0] = reg;
    for (size_t i = 0; i < size; ++i) bytes[i + 1] = data[i];
    return owner_.write_readv(address, bytes.data(), size + 1, nullptr, 0) == 0;
  }
  bool probe(uint8_t address) { return owner_.write_readv(address, nullptr, 0, nullptr, 0) == 0; }
 private:
  Owner &owner_;
};
}  // namespace esphome::hatch_plus_power
