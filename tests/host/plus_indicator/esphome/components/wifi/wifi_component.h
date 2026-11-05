#pragma once
namespace esphome::wifi {
class WiFiComponent {
 public:
  bool connected = false;
  bool is_connected() const { return connected; }
};
inline WiFiComponent *global_wifi_component = nullptr;
}
