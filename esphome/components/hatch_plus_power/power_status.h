#pragma once
#include <cstdint>

namespace esphome::hatch_plus_power {
// Exact 7.1.645 GreenPAK bit addresses: 0x3a5, 0x3a6, 0x3b2.
// These are logic levels, not a guarantee that a battery is charging safely.
struct PowerStatus {
  uint8_t status_74;
  uint8_t status_75;
  uint8_t status_76;
  constexpr bool wireless_voltage_present() const { return (status_74 & 0x20) != 0; }
  constexpr bool external_power_present() const { return (status_74 & 0x40) != 0; }
  constexpr bool charge_enable_logic() const { return (status_76 & 0x04) != 0; }
};
}  // namespace esphome::hatch_plus_power
