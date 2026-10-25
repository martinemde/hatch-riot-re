#pragma once
#include <cstdint>

// Offline stock 7.1.645 ADC/reference stage. Calibration and peripheral IO are
// injected dependencies, not a live hardware adapter. See plus-adc-controller-recovery.md.
namespace esphome::hatch_plus_power {
// Exact 21 entries at stock 0x3f4ac220: {reference, comparator voltage}.
inline constexpr uint32_t ADC_REFERENCE_TABLE[21][2] = {
  {42,2752},{43,2816},{44,2880},{45,2944},{46,3008},{47,3072},{48,3136},
  {49,3200},{50,3264},{51,3328},{52,3392},{53,3456},{54,3520},{55,3584},
  {56,3648},{57,3712},{58,3776},{59,3840},{60,3904},{61,3968},{62,4032}
};
// io.raw(channel), io.calibrate(raw), io.reference(setting) are ordered calls.
// reference returns success; failure preserves the shared last-successful index.
// BatteryState owns comparator_index/updated/deadline, supplied here by reference.
// False means use the comparator scan instead; true returns a sample even when
// reference programming fails, as stock does. There is no settle in this ADC path.
template<class IO>
bool adc_sample(uint8_t flags, uint8_t &comparator_index, uint8_t &updated,
                uint32_t deadline, uint32_t now, IO &io, uint16_t &raw) {
  if (deadline < now) updated = 0;  // unsigned stock comparison, not wrap-aware
  if (!(flags & 2)) return false;
  uint32_t calibrated = io.calibrate(io.raw(3));
  uint32_t product = calibrated * 25U;
  uint32_t scaled = uint32_t((uint64_t(product) * 0x88888889U) >> 32) >> 3;
  raw = uint16_t(scaled);
  uint32_t voltage = scaled - 200U;
  uint8_t selected = comparator_index;
  if (voltage <= 2752U) selected = 0;
  else if (voltage > 4031U) selected = 20;
  else for (uint8_t i = 0; i < 20; ++i) {
    uint32_t lower = ADC_REFERENCE_TABLE[i][1], upper = ADC_REFERENCE_TABLE[i+1][1];
    if (voltage >= lower && voltage < upper) {
      selected = voltage - lower < upper - voltage ? i : i + 1;
      break;
    }
  }
  if (selected != comparator_index && io.reference(ADC_REFERENCE_TABLE[selected][0]))
    comparator_index = selected;
  return true;
}
}  // namespace esphome::hatch_plus_power
