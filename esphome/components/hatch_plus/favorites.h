#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome::hatch_plus {
constexpr size_t FAVORITE_COUNT = 10;
constexpr size_t LEGACY_FAVORITE_COUNT = 8;
constexpr size_t PRESET_NAME_SIZE = 33;
constexpr size_t PRESET_TEXT_SIZE = 129;
constexpr size_t STOCK_SOUND_SIZE = 33;
constexpr size_t URI_SIZE = 256;  // Includes the trailing NUL.
constexpr uint32_t LEGACY_STORAGE_KEY = 0x48504C31;  // HPL1; independent of entity names.
constexpr uint32_t STORAGE_KEY = 0x48504C32;  // HPL2; leave the original record readable for migration.
constexpr uint32_t STORAGE_VERSION = 2;
struct LegacyFavorite {
  uint8_t used{0};
  uint8_t light_on{0};
  uint8_t repeat{0};
  uint8_t reserved{0};
  float red{0}, green{0}, blue{0}, white{0}, brightness{1}, color_brightness{1}, volume{0.5f};
  char uri[URI_SIZE]{};
};
struct LegacyFavoriteStore {
  uint32_t version{1};
  int32_t selected{-1};
  LegacyFavorite favorites[LEGACY_FAVORITE_COUNT]{};
};
struct Favorite : LegacyFavorite {
  uint8_t snapshot{0};
  uint8_t clock_on{1};
  uint8_t clock_light_on{1};
  uint8_t reserved_snapshot{0};
  float clock_brightness{1};
  char name[PRESET_NAME_SIZE]{};
  char text[PRESET_TEXT_SIZE]{};
  char stock_sound[STOCK_SOUND_SIZE]{};
};
struct FavoriteStore {
  uint32_t version{STORAGE_VERSION};
  int32_t selected{-1};
  Favorite favorites[FAVORITE_COUNT]{};
};
inline bool valid_index(int index) { return index >= 0 && index < static_cast<int>(FAVORITE_COUNT); }
inline bool unit_value(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
inline bool valid_uri(const std::string &uri) {
  if (uri.empty()) return true;  // Explicitly light-only.
  if (uri.size() >= URI_SIZE) return false;
  const bool library = uri.compare(0, 17, "hatch-library:///") == 0;
  for (unsigned char c : uri) if (c < 32 || c == 127 || (c == 32 && !library)) return false;
  const char *schemes[] = {"https://", "http://", "hatch-library:///"};
  for (const char *scheme : schemes) {
    size_t n = std::strlen(scheme);
    if (uri.compare(0, n, scheme) == 0 && uri.size() > n) return true;
  }
  return false;
}
inline bool valid_legacy_favorite(const LegacyFavorite &f) {
  if (f.used > 1 || f.light_on > 1 || f.repeat > 1) return false;
  if (!unit_value(f.red) || !unit_value(f.green) || !unit_value(f.blue) ||
      !unit_value(f.white) || !unit_value(f.brightness) || !unit_value(f.color_brightness) || !unit_value(f.volume)) return false;
  if (std::memchr(f.uri, 0, URI_SIZE) == nullptr) return false;
  return valid_uri(f.uri);
}
inline bool valid_preset_string(const std::string &value, size_t capacity) {
  return value.size() < capacity && value.find('\0') == std::string::npos;
}
inline bool valid_favorite(const Favorite &f) {
  return valid_legacy_favorite(f) && f.snapshot <= 1 && f.clock_on <= 1 && f.clock_light_on <= 1 &&
      unit_value(f.clock_brightness) && std::memchr(f.name, 0, sizeof(f.name)) != nullptr &&
      std::memchr(f.text, 0, sizeof(f.text)) != nullptr &&
      std::memchr(f.stock_sound, 0, sizeof(f.stock_sound)) != nullptr &&
      (!f.snapshot || f.stock_sound[0] != 0);
}
inline bool valid_legacy_store(const LegacyFavoriteStore &s) {
  if (s.version != 1 || s.selected < -1 || s.selected >= static_cast<int>(LEGACY_FAVORITE_COUNT)) return false;
  for (const auto &f : s.favorites) if (!valid_legacy_favorite(f)) return false;
  return s.selected == -1 || s.favorites[s.selected].used;
}
inline void migrate_legacy_store(const LegacyFavoriteStore &legacy, FavoriteStore &result) {
  result.selected = legacy.selected;
  for (size_t i = 0; i < LEGACY_FAVORITE_COUNT; ++i)
    static_cast<LegacyFavorite &>(result.favorites[i]) = legacy.favorites[i];
}
inline bool valid_store(const FavoriteStore &s) {
  if (s.version != STORAGE_VERSION || (s.selected != -1 && !valid_index(s.selected))) return false;
  for (const auto &f : s.favorites) if (!valid_favorite(f)) return false;
  return s.selected == -1 || s.favorites[s.selected].used;
}
inline int adjacent_favorite(const FavoriteStore &s, int direction) {
  int start = s.selected;
  if (start < 0) start = direction > 0 ? -1 : 0;
  for (int n = 1; n <= static_cast<int>(FAVORITE_COUNT); n++) {
    int i = (start + direction * n + FAVORITE_COUNT * 2) % FAVORITE_COUNT;
    if (s.favorites[i].used) return i;
  }
  return -1;
}
}  // namespace esphome::hatch_plus
