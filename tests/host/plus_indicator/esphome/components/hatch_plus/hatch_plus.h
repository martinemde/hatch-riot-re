#pragma once
namespace esphome::hatch_plus {
class HatchPlus {
 public:
  bool locked = false;
  bool get_child_lock() const { return locked; }
};
}
