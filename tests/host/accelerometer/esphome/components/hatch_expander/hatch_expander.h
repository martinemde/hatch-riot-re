#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace esphome::hatch_expander {
class HatchExpander {
 public:
  using Write = std::tuple<int, int, std::vector<uint8_t>>;
  using Read = std::tuple<int, int, size_t>;
  std::vector<Write> writes;
  std::vector<Read> reads;
  size_t reads_before_first_write{0};
  std::array<uint8_t, 6> raw{0, 0, 0, 0, 0, 0xC0};
  std::optional<uint8_t> da213_id{0x13};
  std::optional<uint8_t> msa311_id{0x13};
  uint8_t msa311_mode{0x00};
  bool fail_read{false};
  int fail_read_reg{-1};
  int fail_write_reg{-1};

  bool write_device_registers(uint8_t address, uint8_t reg, const uint8_t *data, size_t len) {
    if (writes.empty())
      reads_before_first_write = reads.size();
    writes.emplace_back(address, reg, std::vector<uint8_t>(data, data + len));
    return fail_write_reg != reg;
  }

  bool read_device_registers(uint8_t address, uint8_t reg, uint8_t *data, size_t len) {
    reads.emplace_back(address, reg, len);
    if (fail_read || fail_read_reg == reg || (address != 0x27 && address != 0x62))
      return false;
    const auto &id = address == 0x27 ? da213_id : msa311_id;
    if (!id)
      return false;
    if (reg == 0x02) {
      for (size_t i = 0; i < len; i++)
        data[i] = raw.at(i);
    } else if (reg == 0x01) {
      *data = *id;
    } else if (address == 0x62 && reg == 0x11) {
      *data = msa311_mode;
    } else {
      return false;
    }
    return true;
  }
};
}  // namespace esphome::hatch_expander
