#include "hatch_accelerometer.h"

#include <algorithm>
#include <cassert>

using namespace esphome;
using hatch_accelerometer::AccelerometerModel;
using hatch_accelerometer::HatchAccelerometer;
using hatch_expander::HatchExpander;

static void test_da213_read_only_discovery() {
  HatchExpander bus;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_model(AccelerometerModel::DA213);
  accel.update();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x27, 0x01, 1}}));
  assert(bus.writes.empty());
  assert(!accel.valid());
}

static void test_da213_initialization_and_samples() {
  HatchExpander bus;
  HatchAccelerometer accel;
  sensor::Sensor x, y, z, angle;
  binary_sensor::BinarySensor upright, tilted;
  accel.set_expander(&bus);
  accel.set_model(AccelerometerModel::DA213);
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
  bus.da213_id = 0x12;
  accel.update();
  assert(bus.writes.empty() && !accel.valid());

  bus.da213_id = 0x13;
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

static void test_msa311_read_only_discovery() {
  HatchExpander bus;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_model(AccelerometerModel::MSA311);
  accel.update();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x62, 0x01, 1}, {0x62, 0x11, 1}}));
  assert(bus.writes.empty());
  assert(!accel.valid());
}

static void test_msa311_rejects_unverified_devices() {
  HatchExpander bus;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_model(AccelerometerModel::MSA311);
  accel.set_allow_initialization(true);

  bus.msa311_id = 0x12;
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x62, 0x01, 1}}));
  assert(bus.writes.empty() && !accel.valid());

  const std::vector<HatchExpander::Read> probe{{0x62, 0x01, 1}, {0x62, 0x11, 1}};
  bus.reads.clear();
  bus.msa311_id = 0x13;
  bus.msa311_mode = 0xDE;  // MSA321 shares the MSA311 chip ID.
  accel.update();
  assert(bus.reads == probe);
  assert(bus.writes.empty() && !accel.valid());

  bus.reads.clear();
  bus.msa311_mode = 0x00;
  bus.fail_read_reg = 0x11;
  accel.update();
  assert(bus.reads == probe);
  assert(bus.writes.empty() && !accel.valid());
}

static void test_msa311_initialization_and_samples() {
  HatchExpander bus;
  HatchAccelerometer accel;
  sensor::Sensor x, y, z;
  accel.set_expander(&bus);
  accel.set_model(AccelerometerModel::MSA311);
  accel.set_allow_initialization(true);
  accel.set_acceleration_x(&x);
  accel.set_acceleration_y(&y);
  accel.set_acceleration_z(&z);

  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x62, 0x01, 1}, {0x62, 0x11, 1}}));
  const std::vector<HatchExpander::Write> stock_writes{{0x62, 0x00, {0x24}},
                                                     {0x62, 0x10, {0x2C}},
                                                     {0x62, 0x11, {0x1E}}};
  assert(bus.writes == stock_writes);
  assert(!accel.valid());

  bus.reads.clear();
  // Nonzero low bits distinguish signed division from arithmetic right shift.
  bus.raw = {0x0F, 0x40, 0xE1, 0xDF, 0x00, 0xE0};
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x62, 0x02, 6}}));
  assert(accel.valid() && accel.is_upright());
  assert(x.state == 1.0f && y.state == -513.0f / 1024.0f && z.state == -0.5f);

  // One native count above the -0.5 g upright threshold.
  bus.raw[4] = 0x10;
  accel.update();
  assert(accel.valid() && !accel.is_upright());
  assert(z.state == -511.0f / 1024.0f);
  assert(bus.writes == stock_writes);
}

static void assert_auto_probe_reads(const HatchExpander &bus, bool expect_msa311_mode) {
  std::vector<HatchExpander::Read> expected{{0x27, 0x01, 1}, {0x62, 0x01, 1}};
  if (expect_msa311_mode)
    expected.emplace_back(0x62, 0x11, 1);
  // One call per register to the expander owner; transport retries are inside it.
  assert(std::is_permutation(bus.reads.begin(), bus.reads.end(), expected.begin(), expected.end()));
  if (expect_msa311_mode) {
    const auto msa311_id = std::find(bus.reads.begin(), bus.reads.end(), expected[1]);
    const auto msa311_mode = std::find(bus.reads.begin(), bus.reads.end(), expected[2]);
    assert(msa311_id < msa311_mode);
  }
}

static void test_auto_initializes_unique_da213() {
  HatchExpander bus;
  bus.msa311_id = 0x12;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_allow_initialization(true);

  accel.update();
  assert_auto_probe_reads(bus, false);
  assert(bus.reads_before_first_write == bus.reads.size());
  assert((bus.writes == std::vector<HatchExpander::Write>{{0x27, 0x00, {0x24}}, {0x27, 0x0F, {0x00, 0x03, 0x06}}}));

  bus.reads.clear();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x27, 0x02, 6}}));
  assert(accel.valid() && bus.writes.size() == 2);
}

static void test_auto_initializes_unique_msa311() {
  HatchExpander bus;
  bus.da213_id = 0x12;
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_allow_initialization(true);

  accel.update();
  assert_auto_probe_reads(bus, true);
  assert(bus.reads_before_first_write == bus.reads.size());
  assert((bus.writes == std::vector<HatchExpander::Write>{{0x62, 0x00, {0x24}},
                                                       {0x62, 0x10, {0x2C}},
                                                       {0x62, 0x11, {0x1E}}}));

  bus.reads.clear();
  accel.update();
  assert((bus.reads == std::vector<HatchExpander::Read>{{0x62, 0x02, 6}}));
  assert(accel.valid() && bus.writes.size() == 3);
}

static void assert_auto_stays_unresolved(HatchExpander &bus, bool expect_msa311_mode) {
  HatchAccelerometer accel;
  accel.set_expander(&bus);
  accel.set_allow_initialization(true);

  accel.update();
  assert_auto_probe_reads(bus, expect_msa311_mode);
  assert(bus.writes.empty() && !accel.valid());

  // Even if the bus later has a unique match, AUTO must not reopen discovery.
  bus.reads.clear();
  bus.da213_id = 0x13;
  bus.msa311_id.reset();
  bus.fail_read_reg = -1;
  accel.update();
  accel.update();
  assert(bus.reads.empty() && bus.writes.empty() && !accel.valid());
}

static void test_auto_stops_on_ambiguous_or_missing_devices() {
  HatchExpander both;
  assert_auto_stays_unresolved(both, true);

  HatchExpander neither;
  neither.da213_id.reset();
  neither.msa311_id.reset();
  assert_auto_stays_unresolved(neither, false);
}

static void test_auto_rejects_unverified_msa311() {
  HatchExpander msa321;
  msa321.da213_id.reset();
  msa321.msa311_mode = 0xDE;  // MSA321 shares the MSA311 chip ID.
  assert_auto_stays_unresolved(msa321, true);

  HatchExpander unreadable_mode;
  unreadable_mode.da213_id.reset();
  unreadable_mode.fail_read_reg = 0x11;
  assert_auto_stays_unresolved(unreadable_mode, true);
}

static void test_auto_read_only_discovery() {
  HatchExpander bus;
  bus.da213_id.reset();
  HatchAccelerometer accel;
  accel.set_expander(&bus);

  accel.update();
  accel.update();
  assert_auto_probe_reads(bus, true);
  assert(bus.writes.empty() && !accel.valid());
}

int main() {
  test_da213_read_only_discovery();
  test_da213_initialization_and_samples();
  test_msa311_read_only_discovery();
  test_msa311_rejects_unverified_devices();
  test_msa311_initialization_and_samples();
  test_auto_initializes_unique_da213();
  test_auto_initializes_unique_msa311();
  test_auto_stops_on_ambiguous_or_missing_devices();
  test_auto_rejects_unverified_msa311();
  test_auto_read_only_discovery();
}
