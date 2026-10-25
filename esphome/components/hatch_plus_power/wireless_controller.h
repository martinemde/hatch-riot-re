#pragma once
#include <cmath>
#include <cstdint>

namespace esphome::hatch_plus_power {
// Exact 101-word table at stock 0x3f4ace10, riotPlus 7.1.645.
inline constexpr uint32_t RECEIVER_TEMPERATURE_TABLE[] = {
  368638, 348299, 329218, 311309, 294493, 278697, 263852, 249895, 236769, 224417,
  212791, 201842, 191528, 181807, 172643, 163999, 155843, 148145, 140876, 134011,
  127523, 121390, 115591, 110105, 104914, 100000, 95346, 90938, 86762, 82802,
  79048, 75487, 72108, 68901, 65857, 62965, 60218, 57607, 55125, 52765,
  50520, 48384, 46350, 44415, 42572, 40816, 39143, 37548, 36027, 34577,
  33194, 31874, 30615, 29412, 28263, 27166, 26118, 25116, 24159, 23243,
  22367, 21529, 20728, 19960, 19226, 18522, 17848, 17203, 16584, 15991,
  15422, 14877, 14354, 13853, 13371, 12909, 12466, 12040, 11631, 11238,
  10860, 10497, 10148, 9813, 9491, 9181, 8882, 8595, 8319, 8053,
  7797, 7550, 7312, 7084, 6863, 6651, 6446, 6249, 6058, 5875,
  5698
};
inline int receiver_temperature_scaled(uint32_t value) {
  if (value > 368637) return 0;
  if (value <= 5698) return 100;
  unsigned best = 0;
  uint32_t distance = UINT32_MAX;
  for (unsigned i = 0; i <= 100; ++i) {
    const auto point = RECEIVER_TEMPERATURE_TABLE[i];
    const uint32_t delta = value > point ? value - point : point - value;
    if (delta <= distance) { best = i; distance = delta; }
  }
  return int(best);
}
inline int receiver_temperature(uint8_t first, uint8_t second) {
  return receiver_temperature_scaled(((first & 3U) | (unsigned(second) << 2)) * 488U);
}
// Bus additionally supplies probe(address7). Failed discovery preserves cached
// presence, as stock does. Stock returns its 8-bit address convention (0xc0).
template<class Bus> class WirelessController {
 public:
  explicit WirelessController(Bus &bus, bool known = false) : bus_(bus), known_(known) {}
  int find_receiver() {
    if (!bus_.probe(0x60)) return -2;
    known_ = true; return 0xc0;
  }
  int receiver_status() {
    if (!known_) return -3;
    uint8_t value;
    return bus_.read(0x60, 0x03, &value, 1) ? value : -4;
  }
  int temperature() {
    if (!known_) return -3;
    uint8_t bytes[2];
    if (!bus_.read(0x60, 0x38, bytes, 2)) return -4;
    return receiver_temperature(bytes[0], bytes[1]);
  }
  int clear_receiver_bits(uint8_t reg, uint8_t mask) {
    if (!known_) return -3;
    uint8_t value;
    if (!bus_.read(0x60, reg, &value, 1)) return -4;
    value &= ~mask;
    return bus_.write(0x60, reg, &value, 1) ? 0 : -5;
  }
  int set_receiver_voltage(double voltage) {
    if (!known_) return -3;
    if (voltage > 4.96062 || voltage < 0) return -6;
    // NaN's stock ROM conversion is unproven; reject it without a bus write.
    if (std::isnan(voltage)) return -6;
    const uint8_t value = uint8_t(std::round(voltage / 0.03906));
    return bus_.write(0x60, 0x1d, &value, 1) ? 0 : -5;
  }
 private:
  Bus &bus_;
  bool known_;
};
}  // namespace esphome::hatch_plus_power
