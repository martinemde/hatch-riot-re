#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome::hatch_plus {
constexpr size_t FAVORITE_COUNT = 8;
constexpr size_t URI_SIZE = 256;  // Includes the trailing NUL.
constexpr uint32_t STORAGE_KEY = 0x48504C31;  // HPL1; independent of entity names.
constexpr uint32_t STORAGE_VERSION = 1;
struct Favorite {
  uint8_t used{0};
  uint8_t light_on{0};
  uint8_t repeat{0};
  uint8_t reserved{0};
  float red{0}, green{0}, blue{0}, white{0}, brightness{1}, color_brightness{1}, volume{0.5f};
  char uri[URI_SIZE]{};
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
inline bool valid_favorite(const Favorite &f) {
  if (f.used > 1 || f.light_on > 1 || f.repeat > 1) return false;
  if (!unit_value(f.red) || !unit_value(f.green) || !unit_value(f.blue) ||
      !unit_value(f.white) || !unit_value(f.brightness) || !unit_value(f.color_brightness) || !unit_value(f.volume)) return false;
  if (std::memchr(f.uri, 0, URI_SIZE) == nullptr) return false;
  return valid_uri(f.uri);
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
