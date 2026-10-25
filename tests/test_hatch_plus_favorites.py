"""Host tests for the fixed-layout record and URI/index validation (no hardware)."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_favorite_record(tmp_path):
    source = tmp_path / "favorites_test.cpp"
    source.write_text(r'''#include "favorites.h"
#include <cassert>
#include <limits>
using namespace esphome::hatch_plus;
int main() {
  FavoriteStore s{};
  assert(valid_store(s));
  assert(!valid_index(-1) && valid_index(0) && valid_index(7) && valid_index(8) && valid_index(9) && !valid_index(10));
  assert(adjacent_favorite(s, 1) == -1);
  s.favorites[2].used = s.favorites[6].used = 1;
  assert(adjacent_favorite(s, 1) == 2);
  assert(adjacent_favorite(s, -1) == 6);
  s.selected = 6;
  assert(adjacent_favorite(s, 1) == 2);
  assert(adjacent_favorite(s, -1) == 2);
  s.selected = 2;
  assert(adjacent_favorite(s, -1) == 6);
  s.favorites[6].used = 0;
  assert(adjacent_favorite(s, 1) == 2);
  assert(valid_uri("hatch-library:///rain.mp3"));
  const std::string spaced_uri = "hatch-library:///my rain recording.wav";
  assert(valid_uri(spaced_uri));
  std::memcpy(s.favorites[2].uri, spaced_uri.c_str(), spaced_uri.size() + 1);
  FavoriteStore roundtrip{};
  std::memcpy(&roundtrip, &s, sizeof(s));
  assert(valid_store(roundtrip));
  assert(std::string(roundtrip.favorites[2].uri) == spaced_uri);
  assert(valid_uri("https://example.org/rain.mp3"));
  assert(valid_uri(""));
  assert(!valid_uri("hatch-library:///"));
  assert(!valid_uri("file:///etc/passwd"));
  assert(!valid_uri("http://example.org/a b"));
  assert(!valid_uri(std::string("http://a/") + std::string(250, 'x')));
  assert(!valid_uri(std::string("http://a/") + std::string(1, '\0')));
  assert(valid_store(s));
  s.version = 999; assert(!valid_store(s)); s.version = STORAGE_VERSION;
  s.selected = 7; assert(!valid_store(s)); s.selected = 2;
  s.favorites[2].volume = std::numeric_limits<float>::quiet_NaN();
  assert(!valid_store(s));
  for (float boundary : {0.0f, 1.0f}) {
    s.favorites[2].volume = boundary;
    FavoriteStore persisted{};
    std::memcpy(&persisted, &s, sizeof(s));
    assert(valid_store(persisted) && persisted.favorites[2].volume == boundary);
  }
  s.favorites[2].volume = -0.01f; assert(!valid_store(s));
  s.favorites[2].volume = 1.01f; assert(!valid_store(s));
  s.favorites[2].volume = std::numeric_limits<float>::infinity(); assert(!valid_store(s));
  s.favorites[2].volume = 0.5;
  s.favorites[2].red = 1.1; assert(!valid_store(s)); s.favorites[2].red = 0;
  s.favorites[2].repeat = 2; assert(!valid_store(s)); s.favorites[2].repeat = 0;
  std::memset(s.favorites[2].uri, 'a', URI_SIZE); assert(!valid_store(s));
  return 0;
}
''')
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    exe = tmp_path / "favorites_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "esphome/components/hatch_plus"), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_controller_with_host_interfaces(tmp_path):
    """Run the real controller, not a Python reimplementation of its logic."""
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path / "controller_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(host), "-I", str(component),
                    str(host / "controller_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_all_button_gestures(tmp_path):
    """Compile the real controller and check all 28 button/gesture combinations."""
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path / "gestures_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(host), "-I", str(component),
                    str(host / "gestures_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_named_presets_and_migration(tmp_path):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path / "presets_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DUSE_HATCH_PLUS_PRESETS",
                    "-I", str(host), "-I", str(component),
                    str(host / "presets_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_on_device_scene_buttons(tmp_path):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    host = ROOT / "tests/host/hatch_plus"
    component = ROOT / "esphome/components/hatch_plus"
    exe = tmp_path / "scene_buttons_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DUSE_HATCH_PLUS_PRESETS",
                    "-I", str(host), "-I", str(component),
                    str(host / "scene_buttons_test.cpp"), str(component / "hatch_plus.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
