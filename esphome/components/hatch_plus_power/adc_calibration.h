#pragma once
#include <cstdint>
namespace esphome::hatch_plus_power {
// Layout is stock esp_adc_cal_characteristics_t (ESP32 legacy calibration).
struct AdcCalibration {
  uint32_t unit, attenuation, width, coefficient_a, coefficient_b, vref;
  const uint32_t *low, *high;
};
// Exact stock 401d547c arithmetic. Callers provide valid non-null LUTs when
// attenuation==3 and normalized raw>2879; width must be <=12.
inline uint32_t adc_raw_to_voltage(uint32_t raw, const AdcCalibration &c) {
  uint32_t normalized = raw << (12 - c.width);
  uint32_t x = normalized < 4095 ? normalized : 4095;
  auto linear = [&]() { return ((x * c.coefficient_a + 0x8000U) >> 16) + c.coefficient_b; };
  if (c.attenuation != 3 || normalized <= 2879) return linear();
  uint32_t delta = x - 2880;
  uint32_t index = (delta >> 6) & 65535;
  uint32_t below = ((index + 46) & 65535) * 64U - x;
  uint32_t above = x - (((index + 46) & 65535) * 64U - 64U);
  uint32_t a = 1200U - c.vref, b = c.vref - 1000U;
  uint32_t sum = (a * c.low[index] + b * c.high[index]) * below;
  sum += a * c.low[index+1] * above;
  sum += b * c.high[index+1] * above;
  sum += 6400U;
  int64_t product = int64_t(int32_t(sum)) * int64_t(0x51eb851f);
  int32_t hi = int32_t(product >> 32);
  uint32_t nonlinear = uint32_t((hi >> 12) - (int32_t(sum) >> 31));
  if (normalized > 2944) return nonlinear;
  uint32_t value = linear();
  return ((value << 6) + 32U - value * delta + delta * nonlinear) >> 6;
}
}  // namespace esphome::hatch_plus_power
