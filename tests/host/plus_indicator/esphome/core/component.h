#pragma once

namespace esphome {
class Component {
 public:
  virtual void loop() {}
  virtual void dump_config() {}
};
}  // namespace esphome
