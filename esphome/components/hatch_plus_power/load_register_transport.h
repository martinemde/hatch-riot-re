#pragma once
#include <cstdint>

namespace esphome::hatch_plus_power {
enum class LoadExpander { AW9110B, AW9523B };
// Owner retains its configured address and all output-latch shadow state. GPIOE
// encoded levels1/2 mean low/high. Core owns readback validation and load retries.
// Physical bus failures map to nonzero errno; bool is never a GPIOE success code.
template<class Owner> class LoadRegisterTransport {
 public:
  LoadRegisterTransport(Owner &owner, LoadExpander kind) : owner_(owner), kind_(kind) {}
  int initialize() { return owner_.initialize_load_outputs_status(); }
  int write(uint8_t logical_pin, unsigned encoded) {
    int physical = pin(logical_pin);
    if (physical < 0 || (encoded != 1 && encoded != 2)) return 0x102;
    return owner_.write_output_pin_status(uint8_t(physical), encoded == 2);
  }
  int read(uint8_t logical_pin, uint8_t &level) {
    int physical = pin(logical_pin);
    if (physical < 0) return 0x102;
    bool value;
    const int result = owner_.read_output_pin_status(uint8_t(physical), value);
    if (result) return result;
    level = value; return 0;
  }
 private:
  int pin(uint8_t logical) const {
    if (logical != 0 && logical != 2) return -1;
    return kind_ == LoadExpander::AW9110B ? logical + 8 : logical;
  }
  Owner &owner_;
  LoadExpander kind_;
};
}  // namespace esphome::hatch_plus_power
