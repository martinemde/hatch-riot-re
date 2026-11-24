#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esphome::hatch_accelerometer {

struct Acceleration {
  int16_t counts[3];
  bool upright() const { return this->counts[2] < -0x7FF; }
  float g(unsigned axis) const { return this->counts[axis] / 4096.0f; }
  float tilt_degrees() const {
    const float x = this->counts[0], y = this->counts[1], z = this->counts[2];
    const float norm = std::sqrt(x * x + y * y + z * z);
    if (norm == 0)
      return NAN;
    return std::acos(std::clamp(-z / norm, -1.0f, 1.0f)) * (180.0f / 3.14159265358979323846f);
  }
};

inline Acceleration decode_da213(const uint8_t *raw) {
  Acceleration result{};
  for (unsigned i = 0; i < 3; i++)
    result.counts[i] = int16_t(uint16_t(raw[2 * i]) | (uint16_t(raw[2 * i + 1]) << 8)) / 4;
  return result;
}

}  // namespace esphome::hatch_accelerometer
