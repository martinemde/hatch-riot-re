#pragma once
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
namespace esphome::hatch_plus_power {
class HatchPlusPower {
 public:
  bool ready = true;
  bool succeed = true;
  float battery = std::numeric_limits<float>::quiet_NaN();
  bool charging = false;
  std::vector<std::pair<uint8_t, uint8_t>> attempts;
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  bool indicators_ready() const { return ready; }
  float indicator_battery_percent() const { return battery; }
  bool indicator_charging() const { return charging; }
  bool set_indicator(uint8_t index, uint8_t state) {
    attempts.emplace_back(index, state);
    if (!ready || !succeed) return false;
    writes.emplace_back(index, state);
    return true;
  }
};
}
