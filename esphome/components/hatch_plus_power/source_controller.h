#pragma once
#include <array>
#include <cstdint>
#include "charge_controller.h"
#include "history_policy.h"

namespace esphome::hatch_plus_power {
// Standalone stock source/thermal slice. No live adapter.
struct SourceState {
  uint8_t flags = 0, wired_state = 0, wireless_state = 0, previous_flags = 0;
  uint8_t presentation = 0, flag_907c = 0, flag_907d = 0, flag_907b = 0;
  uint32_t inactive_count = 0;  // word3ffd906c
  uint8_t external_charger = 0;  // app context+194, retained if lock fails
};
struct ThermalHistory {
  std::array<uint8_t, 64> ring{};
  uint32_t total = 0;
  uint8_t index = 0, average = 0;
  void accept_pad(int result, ChargeState &shared) {
    const uint8_t value = static_cast<uint8_t>(result);
    ring.fill(value);
    total = uint32_t(value) * 64;
    index = 0;
    shared.flag_9080 = shared.flag_907f = 0;
    // Stock leaves average9070 stale until a later sample.
  }
  void sample(int result, HistoryPolicy policy = HistoryPolicy::STOCK_NEXT_SLOT) {
    const uint8_t value = static_cast<uint8_t>(result);
    const uint8_t overwritten = ring[index];
    ring[index] = value;
    index = (index + 1) & 63;
    const uint8_t removed = policy == HistoryPolicy::ROLLING_SUM ? overwritten : ring[index];
    total = total - removed + value;
    average = static_cast<uint8_t>(total >> 6);
  }
};
struct SourceResult {
  bool inactive_pulse_omitted = false;
  bool inactive_pulse_attempted = false;
};
// IO: read_pin(bit) returns0/1 or negative on transaction failure; discovery
// returns signed result; receiver_status() returns byte or negative on failure.
// Configuration write results are ignored exactly as stock. Temperature errors
// are deliberately truncated to uint8_t, not treated as a safe measurement.
// inactive_adc_raw(channel) and inactive_adc_to_voltage(raw) are uint32 inputs;
// the latter must use stock calibration data, not an assumed board conversion.
// set/clear use GreenPAK masks and return success (ignored in this source slice).
// app_lock() returns exactly1 on acquisition; other values retain +194.
// source_changed(label) is an abstract notification, not a live queue adapter.
template<class IO>
SourceResult source_step(SourceState &s, ThermalHistory &history, ChargeState &shared, IO &io,
                         HistoryPolicy history_policy = HistoryPolicy::STOCK_NEXT_SLOT) {
  if (s.wireless_state == 0) {
    const int pin = io.read_pin(0x3a5);
    if (pin > 0 && io.find_receiver() >= 1) {
      const int status = io.receiver_status();
      if (status >= 0 && (status & 3) == 3) {
        s.flags |= 4;
        io.clear_receiver_bits(0x15, 2);
        io.set_receiver_voltage(4.6);
        history.accept_pad(io.temperature(), shared);
        s.wireless_state = 1;
      }
    }
  } else if (s.wireless_state == 1) {
    const int pin = io.read_pin(0x3a5);
    if (pin > 0) history.sample(io.temperature(), history_policy);
    else if (pin == 0 && io.find_receiver() < 0) {
      s.flags &= ~4;
      s.wireless_state = 0;
    }
  }
  if (s.wired_state == 0 || s.wired_state == 1) {
    const int pin = io.read_pin(0x3a6);
    if (pin >= 0) {
      if (s.wired_state == 0 && pin != 0) { s.flags |= 1; s.wired_state = 1; }
      else if (s.wired_state == 1 && pin == 0) { s.flags &= ~1; s.wired_state = 0; }
    }
  }
  SourceResult result;
  if (io.app_lock() == 1) {
    s.external_charger = (s.flags & 5) ? 1 : 0;
    io.app_unlock();
  }
  if (s.flags & 5) {
    s.flags &= ~2;
    // Stock401d5adb reads ADC channel3, then calls calibration401d547c.
    // Both operations are explicit transport inputs; multiply wraps uint32.
    const uint32_t raw = io.inactive_adc_raw(3);
    const uint32_t converted = io.inactive_adc_to_voltage(raw);
    if (converted * uint32_t(25) <= 0xf626) {
      ++s.inactive_count;
      if (s.inactive_count >= 2) {
        result.inactive_pulse_attempted = true;
        io.set(4);
        io.clear(4);  // Stock ignores BOTH results, including failed set.
      }
    } else s.inactive_count = 0;
  } else {
    s.flags |= 2;
    s.inactive_count = 0;
  }
  if (s.flags != s.previous_flags) {
    s.presentation &= ~7;
    if (s.flags & 4) { io.source_changed(4); s.presentation = 4; }
    else if (s.flags & 1) { io.source_changed(1); s.presentation = 2; }
    else {
      io.source_changed(2);
      s.flag_907c = s.flag_907d = s.flag_907b = 0;
    }
    s.previous_flags = s.flags;
  }
  return result;
}
}  // namespace esphome::hatch_plus_power
