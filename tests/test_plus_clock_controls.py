"""Execute actual display code and YAML lambdas. No device, HA server or network."""
from pathlib import Path
import shutil
import subprocess

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "tests/host/clock_controls"


class ConfigLoader(yaml.SafeLoader):
    pass


ConfigLoader.add_constructor("!lambda", lambda loader, node: loader.construct_scalar(node))
ConfigLoader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))


def compile_run(tmp_path, name, sources, includes):
    compiler = shutil.which("c++")
    assert compiler, "a host C++ compiler is required"
    exe = tmp_path / name
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    *[arg for path in includes for arg in ("-I", str(path))],
                    *map(str, sources), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


@pytest.mark.parametrize("config_name", ["hatch-plus-example.yaml"])
def test_actual_display_and_config_lambdas(tmp_path, config_name):
    config = yaml.load((ROOT / "esphome" / config_name).read_text(), Loader=ConfigLoader)
    script = next(s for s in config["script"] if s["id"] == "update_display")
    brightness = next((n for n in config.get("number", []) if n["id"] == "display_brightness"), None)
    if brightness:
        assert (brightness["min_value"], brightness["max_value"], brightness["step"]) == (0, 15, 1)
        brightness_action = brightness["set_action"][0]["lambda"]
        boot = config["esphome"]["on_boot"][0]["then"][0]["lambda"]
    else:
        light = next(item for item in config["light"] if item["id"] == "clock_light")
        output = next(item for item in config["output"] if item["id"] == light["output"])
        assert light["platform"] == "monochromatic"
        assert light["gamma_correct"] == 1.0
        assert light["default_transition_length"] == "0s"
        assert light["restore_mode"] == "RESTORE_DEFAULT_ON"
        assert round(light["initial_state"]["brightness"] * 15) == 11
        assert output["type"] == "float"
        # The host harness uses 0..15; ESPHome's float output receives 0..1.
        brightness_action = "const float state = x / 15.0f;\n" + output["write_action"][0]["lambda"]
        # Restored light state is sent through the output, not an on_boot lambda.
        boot = "set_display_brightness(id(display_brightness).state);"
    generated = tmp_path / "config_lambdas.h"
    text_scrolls = "show_scrolling_text(" in script["then"][0]["lambda"]
    generated.write_text("#include <cmath>\n" + f"constexpr bool text_scrolls = {str(text_scrolls).lower()};\n"
                         + "void update_display() {\n" + script["then"][0]["lambda"] + "\n}\n"
                         + "void set_display_brightness(float x) {\n"
                         + brightness_action + "\n}\n"
                         + "void boot_brightness() {\n" + boot + "\n}\n")
    # Verify the real automation routes invoke the compiled lambda (not a copied body).
    assert config["time"][0]["platform"] == "homeassistant"
    assert config["time"][0]["on_time_sync"] == [{"script.execute": "update_display"}]
    assert config["time"][0]["on_time"][0] == {
        "seconds": 0, "then": [{"script.execute": "update_display"}]}
    switch = next(s for s in config["switch"] if s["id"] == "show_clock")
    for event in ("on_turn_on", "on_turn_off"):
        assert switch[event] == [{"script.execute": "update_display"}]
    text = next(t for t in config["text"] if t["id"] == "display_text")
    assert text["on_value"] == [{"script.execute": "update_display"}]
    component = ROOT / "esphome/components/hatch_display"
    compile_run(tmp_path, "clock", [HOST / "clock_test.cpp", component / "hatch_display.cpp"],
                [HOST, component, tmp_path])


def test_light_only_favorite_and_controls(tmp_path):
    component = ROOT / "esphome/components/hatch_plus"
    compile_run(tmp_path, "light", [HOST / "light_test.cpp", component / "hatch_plus.cpp"],
                [ROOT / "tests/host/hatch_plus", component])


def test_stock_sound_selection_loops_and_custom_toggle(tmp_path):
    config = yaml.load((ROOT / "esphome/hatch-plus-example.yaml").read_text(), Loader=ConfigLoader)
    select = next(item for item in config["select"] if item["id"] == "stock_sound")
    switch = next(item for item in config["switch"] if item["id"] == "loop_sound")
    source = tmp_path / "stock_sound.cpp"
    source.write_text(r"""
#include <cassert>
#include <string>
#include <vector>
namespace media_player {
enum Command { MEDIA_PLAYER_COMMAND_STOP, MEDIA_PLAYER_COMMAND_REPEAT_ONE,
               MEDIA_PLAYER_COMMAND_REPEAT_OFF };
}
std::vector<std::string> events;
namespace sendspin { enum class SendspinGoodbyeReason { USER_REQUEST }; }
struct SendspinHub {
  void disconnect_from_server(sendspin::SendspinGoodbyeReason reason) {
    assert(reason == sendspin::SendspinGoodbyeReason::USER_REQUEST);
    events.push_back("disconnect");
  }
} sendspin_hub;
struct Player {
  struct Call {
    std::string event;
    Call &set_command(media_player::Command command) {
      event = command == media_player::MEDIA_PLAYER_COMMAND_STOP ? "stop" :
              command == media_player::MEDIA_PLAYER_COMMAND_REPEAT_ONE ? "repeat_one" : "repeat_off";
      return *this;
    }
    Call &set_media_url(const std::string &url) { event = url; return *this; }
    void perform() { events.push_back(event); }
  };
  Call make_call() { return {}; }
} player;
struct LoopSwitch {
  bool state{false};
  void turn_on();
  void turn_off();
} loop_sound;
#define id(name) name
void on_turn_on() {
""" + switch["on_turn_on"][0]["lambda"] + r"""
}
void on_turn_off() {
""" + switch["on_turn_off"][0]["lambda"] + r"""
}
void LoopSwitch::turn_on() { if (!state) { state = true; on_turn_on(); } }
void LoopSwitch::turn_off() { if (state) { state = false; on_turn_off(); } }
void choose_stock(const std::string &x) {
""" + select["set_action"][0]["lambda"] + r"""
}
int main() {
  const std::vector<std::string> options = {
""" + ",".join('"' + option + '"' for option in select["options"] if option != "None") + r"""
  };
  for (const auto &option : options) {
    // Test both a switch transition and an already-on reselect with no callback.
    for (bool initially_on : {false, true}) {
      loop_sound.state = initially_on;
      events.clear(); choose_stock(option);
      assert(loop_sound.state);
      assert(events.size() == (initially_on ? 3 : 4));
      assert(events.front() == "disconnect");
      for (size_t i = 1; i + 1 < events.size(); ++i) assert(events[i] == "repeat_one");
      assert(events.back().find("hatch-library:///Rest_Sounds/wav/") == 0);
      assert(events.back().rfind(".wav") == events.back().size() - 4);
    }
    // The normal switch remains usable for custom playback or opting out afterward.
    events.clear(); loop_sound.turn_off();
    assert(!loop_sound.state && events == std::vector<std::string>{"repeat_off"});
    events.clear(); loop_sound.turn_on();
    assert(loop_sound.state && events == std::vector<std::string>{"repeat_one"});
  }
  events.clear(); choose_stock("None");
  assert((events == std::vector<std::string>{"disconnect", "stop"}));
}
""")
    compile_run(tmp_path, "stock_sound", [source], [])
