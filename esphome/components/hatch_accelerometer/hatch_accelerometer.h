#pragma once
#include "esphome/core/component.h"
#include "esphome/components/hatch_expander/hatch_expander.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "da213.h"
namespace esphome::hatch_accelerometer {
class HatchAccelerometer : public PollingComponent {
 public:
  void set_expander(hatch_expander::HatchExpander *v) { expander_=v; }
  void set_allow_initialization(bool v) { allow_initialization_=v; }
  void set_acceleration_x(sensor::Sensor *v) { axes_[0]=v; }
  void set_acceleration_y(sensor::Sensor *v) { axes_[1]=v; }
  void set_acceleration_z(sensor::Sensor *v) { axes_[2]=v; }
  void set_tilt_angle(sensor::Sensor *v) { tilt_=v; }
  void set_upright(binary_sensor::BinarySensor *v) { upright_=v; }
  void set_not_upright(binary_sensor::BinarySensor *v) { not_upright_=v; }
  bool valid() const { return valid_; }
  bool is_upright() const { return acceleration_.upright(); }
  void update() override;
  void dump_config() override;
  void on_shutdown() override { stopped_=true; invalidate_(); }
 protected:
  void invalidate_();
  hatch_expander::HatchExpander *expander_{nullptr};
  sensor::Sensor *axes_[3]{}, *tilt_{nullptr};
  binary_sensor::BinarySensor *upright_{nullptr}, *not_upright_{nullptr};
  Acceleration acceleration_{};
  bool stopped_{false}, allow_initialization_{false}, initialized_{false}, valid_{false};
};
}
