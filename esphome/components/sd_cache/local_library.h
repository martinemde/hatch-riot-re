#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

namespace esphome::sd_cache {

struct LibraryEntry {
  std::string name;
  std::string uri;  // File: playable URI. Directory: absolute library path for library_page().
  bool is_directory;
  uint64_t size_bytes;
};
struct LibraryPage {
  std::vector<LibraryEntry> entries;
  uint32_t next_offset{0};
  bool has_more{false};
  std::string error;
};

namespace library {
inline constexpr const char *SCHEME = "hatch-library://";
inline constexpr const char *ROOTS[] = {
    "/Rest_Sounds/wav", "/Restore_Sounds", "/RestMini_Sounds", "/Sleep_Sounds", "/Downloaded", "/Alarm_Sounds"};

// Literal paths only: no escaping/normalization and no URI query or fragment syntax.
inline bool clean_path(const std::string &path) {
  if (path.empty() || path.size() > 240 || path[0] != '/' || path.back() == '/')
    return false;
  size_t start = 1;
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') {
      const auto part = path.substr(start, i - start);
      if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
        return false;
      start = i + 1;
    } else {
      const auto c = static_cast<unsigned char>(path[i]);
      if (c < 32 || c >= 127 || c == '\\' || c == '%' || c == '?' || c == '#' || c == ':')
        return false;
    }
  }
  return true;
}
inline bool allowed_directory(const std::string &path) {
  if (path == "/")
    return true;
  if (!clean_path(path))
    return false;
  for (const auto *root : ROOTS) {
    const std::string r(root);
    if (path == r || r.starts_with(path + "/"))
      return true;
    // Downloaded only exposes direct audio files, never its backup or other subdirectories.
    if (r != "/Downloaded" && path.starts_with(r + "/"))
      return true;
  }
  return false;
}
inline bool allowed_file(const std::string &path) {
  if (!clean_path(path))
    return false;
  const auto slash = path.rfind('/');
  const auto parent = path.substr(0, slash);
  if (parent == "/" || !allowed_directory(parent))
    return false;
  bool under_root = false;
  for (const auto *root : ROOTS)
    under_root |= path.starts_with(std::string(root) + "/");
  if (!under_root)
    return false;
  auto dot = path.rfind('.');
  if (dot == std::string::npos || dot < slash)
    return false;
  std::string ext = path.substr(dot + 1);
  for (auto &c : ext)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return ext == "wav" || ext == "mp3" || ext == "flac";
}
inline std::string uri_path(const std::string &uri) {
  if (!uri.starts_with(SCHEME))
    return "";
  auto path = uri.substr(std::string(SCHEME).size());
  return allowed_file(path) ? path : "";
}

// Uses only read operations. FAT has no symbolic links; base is the existing SD FAT mount.
// Cursor counts physical records. At most 8192 skipped + 128 examined + one lookahead per call.
inline LibraryPage page(const std::string &base, const std::string &directory, int32_t offset, int32_t limit) {
  LibraryPage result;
  if (!allowed_directory(directory) || offset < 0 || offset > 8192 || limit < 1 || limit > 64) {
    result.error = "Invalid directory, offset (0..8192), or limit (1..64)";
    return result;
  }
  result.next_offset = uint32_t(offset);
  DIR *dir = opendir((base + directory).c_str());
  if (dir == nullptr) {
    result.error = "Directory unavailable";
    return result;
  }
  uint32_t skipped = 0;
  while (skipped < uint32_t(offset) && readdir(dir) != nullptr)
    skipped++;
  uint32_t examined = 0;
  while (examined < 128 && result.entries.size() < size_t(limit)) {
    auto *entry = readdir(dir);
    if (entry == nullptr)
      break;
    examined++;
    result.next_offset++;
    const std::string name(entry->d_name);
    const std::string path = (directory == "/" ? "" : directory) + "/" + name;
    if (!clean_path(path))
      continue;
    struct stat st;
    if (stat((base + path).c_str(), &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode) && allowed_directory(path)) {
      result.entries.push_back({name, path, true, 0});
    } else if (S_ISREG(st.st_mode) && st.st_size > 44 && allowed_file(path)) {
      result.entries.push_back({name, std::string(SCHEME) + path, false, uint64_t(st.st_size)});
    }
  }
  result.has_more = readdir(dir) != nullptr;
  closedir(dir);
  return result;
}
}  // namespace library
}  // namespace esphome::sd_cache
