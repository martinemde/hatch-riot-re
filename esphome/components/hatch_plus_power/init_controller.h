#pragma once
#include <cstdint>

namespace esphome::hatch_plus_power {
// Offline stock401d56b4 slice. No live hardware adapter.
struct InitState {
  uint8_t flag_9084 = 0;
  uint32_t timer_9088 = 0;
};
// characterize() owns the opaque calibration structure at stock3ffd9048.
// Return values are intentionally ignored: stock does not branch on errors.
template<class IO> void power_init(InitState &s, IO &io) {
  if (io.variant() != 2) return;
  io.adc_width(12);
  io.characterize(0, 3, 12, 1100);
  io.adc_attenuation(3, 3);
  io.configure_virtual(0, 1, 0x7a, 0x20);
  s.flag_9084 = 0;
  s.timer_9088 = 0;
}
}  // namespace esphome::hatch_plus_power
