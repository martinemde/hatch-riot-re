#pragma once
#include "light_state.h"
namespace esphome::light {
class LightEffect {
 public:
  explicit LightEffect(const char *) {}
  virtual ~LightEffect() = default;
  virtual void start() {}
  virtual void stop() {}
  virtual void apply() = 0;
  void init_internal(LightState *state) { state_ = state; }
 protected:
  LightState *state_ = nullptr;
};
}
