#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace esphome::hatch_expander {
class HatchExpander {
 public:
  using Write = std::tuple<int, int, std::vector<uint8_t>>;
  using Read = std::tuple<int, int, size_t>;
  std::vector<Write> writes;
  std::vector<Read> reads;
  std::array<uint8_t, 6> raw{0, 0, 0, 0, 0, 0xC0};
  uint8_t id{0x13};
  bool fail_read{false};
  int fail_write_reg{-1};

  bool write_device_registers(uint8_t address, uint8_t reg, const uint8_t *data, size_t len) {
    writes.emplace_back(address, reg, std::vector<uint8_t>(data, data + len));
    return fail_write_reg != reg;
  }

  bool read_device_registers(uint8_t address, uint8_t reg, uint8_t *data, size_t len) {
    reads.emplace_back(address, reg, len);
    if (fail_read)
      return false;
    if (reg == 0x02) {
      for (size_t i = 0; i < len; i++)
        data[i] = raw.at(i);
    } else {
      *data = id;
    }
    return true;
  }
};
}  // namespace esphome::hatch_expander
