#pragma once

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace esphome::hatch_plus_power {
class HatchPlusPower {
 public:
  bool ready = false;
  bool succeed = true;
  std::vector<std::pair<uint8_t, uint8_t>> attempts;

  bool indicators_ready() const { return ready; }
  bool set_indicator(uint8_t index, uint8_t color) {
    assert(ready);
    attempts.emplace_back(index, color);
    return succeed;
  }
};
}  // namespace esphome::hatch_plus_power
