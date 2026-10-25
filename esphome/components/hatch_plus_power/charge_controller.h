#pragma once
#include <cstdint>

// Standalone charge core only. Not wired to the live diagnostic component.
// Charge subroutine only: stock 7.1.645, 0x401d57dd..0x401d5f2f.
namespace esphome::hatch_plus_power {
struct ChargeState {
  uint8_t state = 0;       // stock byte 0x3ffd8ffc; deliberately numeric
  uint8_t flag_9080 = 0;
  uint8_t flag_907f = 0;
};
struct ChargeInputs {
  uint8_t source_flags;    // stock 0x3ffd9071, supplied after source processing
  uint8_t mode;            // amplifier enabled | (cached LED enabled << 1)
  uint8_t temperature;     // stock averaged byte 0x3ffd9070, NOT assumed Celsius
};
struct ChargeResult {
  uint8_t chip_status;     // stock status field: 1 on bit !=0 OR read failure, else 2
  bool transaction_failed;
};
// Transport contract: read(bit) returns 0/1 or -1 on failure;
// set(mask)/clear(mask) return true on success. Calls are ordered, never rolled back.
// A failure may leave hardware changed even when the software state is unchanged.
template<class Transport, class Mode> ChargeResult charge_step_dynamic(ChargeState &s, ChargeInputs in, Transport &io, Mode mode) {
  bool failed = false;
  auto read = [&](uint16_t bit) {
    int value = io.read(bit);
    if (value < 0) failed = true;
    return value;
  };
  auto set = [&](uint8_t mask) {
    bool ok = io.set(mask); failed |= !ok; return ok;
  };
  auto clear = [&](uint8_t mask) {
    bool ok = io.clear(mask); failed |= !ok; return ok;
  };
  auto start = [&]() {
    if (!clear(0x18) || !clear(0x20) || !set(0x20)) return false;
    return read(0x3b2) == 1;
  };
  if (in.source_flags & 2) s.state = 0; // 0x401d57e0
  const uint8_t chip = read(0x3a8) == 0 ? 2 : 1;
  in.mode = mode();
  auto level2 = [&](uint8_t next) { if (set(0x08)) s.state = next; };
  auto off = [&](uint8_t mask, uint8_t next) { if (clear(mask)) s.state = next; };
  switch (s.state) {
    case 0:
      if (in.source_flags & 4) s.state = 1;
      else if (in.source_flags & 1) s.state = 5;
      break;
    case 1:
      if (start()) s.state = 2;
      break;
    case 2:
      // Stock treats -1 as nonzero here (unlike start's explicit failure check).
      if (read(0x3b2) == 0) { s.state = 0; break; }
      if (chip != 2 || in.temperature > 60) break;
      if (s.flag_9080 != 1 && in.mode != 3) {
        if (!set(0x10)) break;
        s.state = 3;
      }
      if (s.flag_907f != 1 && in.mode == 0) level2(4);
      break;
    case 3:
      if (read(0x3b2) == 0) { s.state = 0; break; }
      if (chip != 2) { off(0x18, 2); break; }
      if (in.temperature > 79) {
        if (clear(0x10)) { s.flag_9080 = 1; s.state = 2; }
      } else if (s.flag_907f != 1 && in.mode == 0) level2(4);
      else if (in.mode == 3) off(0x10, 2);
      break;
    case 4:
      if (read(0x3b2) == 0) { s.state = 0; break; }
      if (chip != 2) { off(0x18, 2); break; }
      if (in.temperature > 79) {
        if (clear(0x18)) { s.flag_907f = 1; s.state = 2; }
      } else if (in.mode == 1 || in.mode == 2) off(0x08, 3);
      else if (in.mode == 3) off(0x18, 2);
      break;
    case 5:
      if (start()) s.state = 6;
      break;
    case 6:
      if (chip == 2 && in.mode != 3 && set(0x10)) {
        s.state = 7;
        if (in.mode == 0) level2(8);
      }
      break;
    case 7:
      if (chip != 2) off(0x18, 6);
      else if (in.mode == 0) level2(8);
      else if (in.mode == 3) off(0x10, 6);
      break;
    case 8:
      if (chip != 2) { off(0x18, 6); break; }
      if (in.mode != 0 && clear(0x08)) {
        s.state = 7;
        if (in.mode == 3) off(0x10, 6);
      }
      break;
    default: break; // stock unknown numeric states retain their value
  }
  return {chip, failed};
}
template<class Transport> ChargeResult charge_step(ChargeState &s, ChargeInputs in, Transport &io) {
  return charge_step_dynamic(s, in, io, [&]() { return in.mode; });
}
}  // namespace esphome::hatch_plus_power
