#include "hatch_accelerometer.h"

#include <cassert>

using namespace esphome;
using hatch_accelerometer::HatchAccelerometer;
using hatch_expander::HatchExpander;

static void test_read_only_discovery() {
  HatchExpander bus;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.update();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x27, 0x01, 1}}));
  assert(bus.writes.empty());
  assert(!accel.valid());
}

static void test_initialization_and_samples() {
  HatchExpander bus;
  HatchAccelerometer accel;
  sensor::Sensor x, y, z, angle;
  binary_sensor::BinarySensor upright, tilted;
  accel.set_expander(&bus);
  accel.set_allow_initialization(true);
  accel.set_acceleration_x(&x);
  accel.set_acceleration_y(&y);
  accel.set_acceleration_z(&z);
  accel.set_tilt_angle(&angle);
  accel.set_upright(&upright);
  accel.set_not_upright(&tilted);

  bus.fail_read = true;
  accel.update();
  assert(bus.writes.empty() && !accel.valid());
  bus.fail_read = false;
  bus.id = 0x12;
  accel.update();
  assert(bus.writes.empty() && !accel.valid());

  bus.id = 0x13;
  bus.fail_write_reg = 0x00;
  accel.update();
  assert((bus.writes == std::vector<HatchExpander::Write>{{0x27, 0x00, {0x24}}}));
  assert(!accel.valid());
  bus.fail_write_reg = -1;
  bus.writes.clear();
  bus.reads.clear();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x27, 0x01, 1}}));
  assert((bus.writes == std::vector<HatchExpander::Write>{{0x27, 0x00, {0x24}}, {0x27, 0x0F, {0x00, 0x03, 0x06}}}));
  assert(!accel.valid());

  accel.update();
  assert(bus.reads.back() == HatchExpander::Read(0x27, 0x02, 6));
  assert(accel.valid() && accel.is_upright());
  assert(upright.valid && upright.state && tilted.valid && !tilted.state);
  assert(x.state == 0.0f && y.state == 0.0f && z.state == -1.0f && angle.state == 0.0f);

  bus.raw = {0, 0x40, 0, 0xC0, 0, 0};
  accel.update();
  assert(accel.valid() && !accel.is_upright());
  assert(x.state == 1.0f && y.state == -1.0f && std::fabs(angle.state - 90.0f) < 0.001f);
  assert(!upright.state && tilted.state);
  bus.raw = {0, 0, 0, 0, 0, 0x40};
  accel.update();
  assert(std::fabs(angle.state - 180.0f) < 0.001f);
  bus.raw = {0, 0, 0, 0, 0, 0};
  accel.update();
  assert(std::isnan(angle.state));

  // Stock upright threshold: -2047 counts is not upright; -2048 is upright.
  bus.raw = {0, 0, 0, 0, 4, 0xE0};
  accel.update();
  assert(!accel.is_upright());
  bus.raw[4] = 0;
  accel.update();
  assert(accel.is_upright());
  // The stock signed division truncates -1 raw to 0 counts.
  bus.raw = {0, 0, 0, 0, 0xFF, 0xFF};
  accel.update();
  assert(z.state == 0.0f);

  bus.fail_read = true;
  accel.update();
  assert(!accel.valid() && std::isnan(z.state) && std::isnan(angle.state));
  assert(!upright.valid && !tilted.valid);
  bus.fail_read = false;
  accel.update();
  assert(accel.valid() && upright.valid && tilted.valid);
  assert(bus.writes.size() == 2);

  accel.on_shutdown();
  bus.reads.clear();
  accel.update();
  assert(!accel.valid() && !upright.valid && !tilted.valid);
  assert(bus.reads.empty());
}

int main() {
  test_read_only_discovery();
  test_initialization_and_samples();
}
