#pragma once
#include <array>
#include <cstdint>

// Offline stock 7.1.645 battery core. Deliberately not wired to live hardware.
// Evidence: work/riotplus645/power-cfg.txt, 401d5f3a..658b.
namespace esphome::hatch_plus_power {
struct BatteryState {
  std::array<uint16_t, 64> samples{};
  uint16_t index = 0;
  uint32_t total = 0;
  bool initialized = false;
  uint16_t reported = 0;
  bool critical = false;
  uint8_t external_critical_latch = 0;
  uint8_t comparator_index = 255;
  uint32_t accepted = 0, count = 0;
  uint8_t updated = 0;
  uint32_t deadline = 0;
};
inline uint32_t battery_candidate(BatteryState &s, uint32_t candidate) {
  if (candidate - 11 > 83 && s.count >= 6) s.count = 5;
  if (s.count) {
    uint32_t result = s.accepted;
    s.count += result == candidate ? 1 : uint32_t(-1);
    return result;
  }
  s.accepted = candidate; s.count = 1; return candidate;
}
// io.read() returns 0/1 or -1 on error; io.reference(setting) returns success.
// Failed transport preserves the last successful reference, not a rollback.
template<class IO> bool battery_measure(BatteryState &s, IO &io, bool force_low, uint16_t &raw) {
  auto write = [&](uint8_t index) {
    if (!io.reference(42 + index)) return false;
    s.comparator_index = index; return true;
  };
  if (s.comparator_index == 255 && !write(10)) return false;
  int high = io.read();
  if (high < 0) return false;
  uint16_t value = 0;
  if (high) {
    if (s.comparator_index == 20) value = 4232;
    else for (unsigned i = s.comparator_index + 1; i <= 20; ++i) {
      if (!write(i)) return false;
      high = io.read(); if (high < 0) return false;
      if (!high) {
        if (!write(i - 1)) return false;
        value = 2952 + 64 * s.comparator_index; break;
      }
      if (i == 20) value = 2952 + 64 * i;
    }
  } else {
    if (s.comparator_index == 0) value = 2952;
    else for (int i = s.comparator_index - 1; i >= 0; --i) {
      if (!write(i)) return false;
      high = io.read(); if (high < 0) return false;
      if (high || i == 0) { value = 2952 + 64 * i; break; }
    }
  }
  raw = force_low ? 2952 : value; return true;
}
// This covers reporting and the false-reading reset decision, not later shutdown.
// io.reset() returns success. All constants are literal stock arithmetic.
template<class IO, class Provider> void battery_sample_dynamic(BatteryState &s, uint16_t raw, uint8_t flags,
                                                               Provider &provider, IO &io) {
  if (!s.initialized) {
    s.samples.fill(raw); s.total = uint32_t(raw) << 6; s.initialized = true;
  }
  s.samples[s.index] = raw; s.index = (s.index + 1) & 63;
  s.total = s.total - s.samples[s.index] + raw;
  if (provider.board() != 1) return;
  uint16_t average = (s.total >> 6) & 65535;
  if (raw == 2952 || (raw == 4232 && average <= 4099)) {
    if ((flags & 5) && !provider.reset_guard() && io.reset()) s.comparator_index = 255;
    return;
  }
  if (average > 4099) s.reported = 100;
  else if (average <= 3379) s.reported = 0;
  else {
    uint32_t intermediate = 100U * average - 338000U;
    uint32_t candidate = (uint64_t(intermediate >> 4) * 0x05b05b06U) >> 32;
    bool rounded = intermediate <= 0x10b2f && candidate >= 11 && candidate <= 94;
    if (rounded) {
      candidate = ((((uint64_t(candidate + 2) * 0xcccccccdU) >> 32) >> 2) & 65535) * 5;
      uint32_t result = battery_candidate(s, candidate);
      int delta = int(s.reported) - int(result);
      if (delta >= 4 || delta <= -4) s.reported = result;
    } else {
      if (intermediate > 0x10b2f) s.external_critical_latch = 0;
      uint32_t result = battery_candidate(s, candidate);
      if ((result < s.reported && (flags & 2)) ||
          (result > s.reported && !(flags & 2))) s.reported = result;
    }
  }
  s.critical = average <= 3330;
  s.updated = 1; s.deadline = provider.tick() + 3600000U;
}
template<class IO> void battery_sample(BatteryState &s, uint16_t raw, uint8_t flags,
                                      int board, uint32_t reset_guard, uint32_t now, IO &io) {
  struct Provider {
    int board_value;
    uint32_t guard_value, now_value;
    int board() { return board_value; }
    uint32_t reset_guard() { return guard_value; }
    uint32_t tick() { return now_value; }
  } provider{board, reset_guard, now};
  battery_sample_dynamic(s, raw, flags, provider, io);
}
}  // namespace esphome::hatch_plus_power
