#pragma once

#include "da213.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/hatch_expander/hatch_expander.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome::hatch_accelerometer {

class HatchAccelerometer : public PollingComponent {
 public:
  void set_expander(hatch_expander::HatchExpander *expander) { this->expander_ = expander; }
  void set_allow_initialization(bool allow) { this->allow_initialization_ = allow; }
  void set_acceleration_x(sensor::Sensor *axis) { this->axes_[0] = axis; }
  void set_acceleration_y(sensor::Sensor *axis) { this->axes_[1] = axis; }
  void set_acceleration_z(sensor::Sensor *axis) { this->axes_[2] = axis; }
  void set_tilt_angle(sensor::Sensor *tilt) { this->tilt_ = tilt; }
  void set_upright(binary_sensor::BinarySensor *upright) { this->upright_ = upright; }
  void set_not_upright(binary_sensor::BinarySensor *not_upright) { this->not_upright_ = not_upright; }

  // Check valid() before using the last measured orientation.
  bool valid() const { return this->valid_; }
  bool is_upright() const { return this->acceleration_.upright(); }
  void update() override;
  void dump_config() override;
  void on_shutdown() override {
    this->stopped_ = true;
    this->invalidate_();
  }

 protected:
  void invalidate_();
  hatch_expander::HatchExpander *expander_{nullptr};
  sensor::Sensor *axes_[3]{};
  sensor::Sensor *tilt_{nullptr};
  binary_sensor::BinarySensor *upright_{nullptr};
  binary_sensor::BinarySensor *not_upright_{nullptr};
  Acceleration acceleration_{};
  bool stopped_{false};
  bool allow_initialization_{false};
  bool setup_complete_{false};
  bool valid_{false};
};

}  // namespace esphome::hatch_accelerometer
