#pragma once
#include <cstdint>
namespace esphome {
namespace setup_priority { constexpr float DATA = 600.0f; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
}
