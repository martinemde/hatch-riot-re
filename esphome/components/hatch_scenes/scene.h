#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::hatch_scenes {

// Nine slots so every label ("SAV9", "FAV9") fits the four-digit clock.
constexpr int SLOT_COUNT = 9;
constexpr size_t URI_SIZE = 256;  // Including the trailing NUL.

/// One saved slot: the night light, and what the speaker plays (an empty URI is silence).
struct Scene {
  bool saved{false};
  bool light_on{false};
  bool repeat{false};
  uint8_t color_mode{0};
  float brightness{1}, color_brightness{1}, red{1}, green{1}, blue{1}, white{1};
  float volume{0.5f};
  char uri[URI_SIZE]{};
};

using Scenes = std::array<Scene, SLOT_COUNT>;

// Slots are numbered from 1; 0 means no slot.

inline int first_empty(const Scenes &scenes) {
  for (int slot = 1; slot <= SLOT_COUNT; slot++)
    if (!scenes[slot - 1].saved) return slot;
  return 0;
}

/// The nearest saved slot after `from` in direction `step` (+1 or -1), wrapping round.
/// From no slot, forward starts at the first slot and backward at the last.
inline int adjacent_saved(const Scenes &scenes, int from, int step) {
  const int start = from != 0 ? from : (step > 0 ? 0 : SLOT_COUNT + 1);
  for (int n = 1; n <= SLOT_COUNT; n++) {
    const int slot = ((start - 1 + step * n) % SLOT_COUNT + SLOT_COUNT) % SLOT_COUNT + 1;
    if (scenes[slot - 1].saved) return slot;
  }
  return 0;
}

}  // namespace esphome::hatch_scenes
