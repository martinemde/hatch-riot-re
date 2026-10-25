#include "esphome/components/hatch_plus_power/power_controller.h"
#include "esphome/components/hatch_plus_power/charge_controller.h"
#include "esphome/components/hatch_plus_power/runtime_adapter.h"
#include <cassert>
#include <utility>
#include <vector>

using namespace esphome::hatch_plus_power;

struct Transport {
  std::vector<std::pair<char, int>> calls;
  int chip = 0;
  int enabled = 1;
  int fail_at = -1;
  int outputs = 0x38;
  bool record(char op, int value) {
    calls.emplace_back(op, value);
    return static_cast<int>(calls.size()) - 1 != fail_at;
  }
  int read(uint16_t bit) {
    if (!record('r', bit)) return -1;
    return bit == 0x3a8 ? chip : enabled;
  }
  bool set(uint8_t mask) {
    if (!record('s', mask)) return false;
    outputs |= mask;
    return true;
  }
  bool clear(uint8_t mask) {
    if (!record('c', mask)) return false;
    outputs &= ~mask;
    return true;
  }
};

struct TaskIO : ShutdownTransport {
  std::vector<char> stages;
  int wireless = 0, wired = 0, measurement = 1;
  bool reference_ok = true, reset_ok = true;
  uint32_t calibrated = 2160;
  unsigned resets = 0, warnings = 0;
  int board_variant = 2, lock_result = 1;
  int app_lock() { return lock_result; }
  void app_unlock() {}
  int lock() { stages.push_back('L'); return lock_result; }
  void unlock() { stages.push_back('U'); }
  std::vector<unsigned> init_calls;
  int variant() { return board_variant; }
  void adc_width(unsigned width) { init_calls.push_back(width); }
  void characterize(unsigned unit, unsigned attenuation, unsigned width, unsigned vref) {
    assert(unit == 0 && attenuation == 3 && width == 12 && vref == 1100);
    init_calls.push_back(vref);
  }
  void adc_attenuation(unsigned channel, unsigned attenuation) {
    assert(channel == 3 && attenuation == 3); init_calls.push_back(channel);
  }
  void configure_virtual(unsigned bus, unsigned chip, unsigned reg, unsigned mask) {
    assert(bus == 0 && chip == 1 && reg == 0x7a && mask == 0x20);
    init_calls.push_back(mask);
  }
  int read_pin(unsigned bit) { stages.push_back('S'); return bit == 0x3a5 ? wireless : wired; }
  int find_receiver() { return -1; }
  int receiver_status() { return 3; }
  void clear_receiver_bits(int, int) {}
  void set_receiver_voltage(double) {}
  int temperature() { return 25; }
  uint32_t inactive_adc_raw(int) { return 1; }
  uint32_t inactive_adc_to_voltage(uint32_t) { return 3000; }
  void source_changed(int) {}
  bool set(uint8_t) { return true; }
  bool clear(uint8_t) { return true; }
  int read(uint16_t) { stages.push_back('C'); return 0; }
  int read() { stages.push_back('M'); return measurement; }
  uint32_t raw(int channel) { assert(channel == 3); stages.push_back('A'); return 1; }
  uint32_t calibrate(uint32_t) { return calibrated; }
  bool reference(unsigned) { return reference_ok; }
  bool reset() { ++resets; return reset_ok; }
  int read(unsigned, uint8_t &value) override { stages.push_back('O'); value = 0; return 0; }
  void display(bool) override {}
  void prepare() override {}
  void state_shutdown(unsigned) override {}
  void drain_logs() override {}
  int set_virtual(unsigned) override { return 1; }
  void delay(unsigned) override {}
  void external_critical() override { ++warnings; }
  int reset_greenpak() override { ++resets; return reset_ok ? 0 : 1; }
};

struct RuntimeApplication : TaskIO {
  unsigned service_calls = 0;
  void service(uint32_t index, uint32_t) { assert(index != 3); ++service_calls; }
  int runtime_variant = 2;
  int variant() { return runtime_variant; }
  bool amplifier() { return false; }
  bool led_wait() { return false; }
  uint32_t tick() { return 0; }
  int battery_report_lock() { return 1; }
  void battery_report_unlock() {}
  uint32_t reset_guard() { return 0; }
  bool disable_critical() { return false; }
};
struct RuntimeBus {
  uint8_t memory[256]{};
  unsigned transactions = 0;
  bool read_device_registers(uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    assert(address == 0x10); ++transactions;
    for (size_t n = 0; n < size; ++n) data[n] = memory[reg + n];
    return true;
  }
  bool write_device_registers(uint8_t address, uint8_t reg, const uint8_t *data, size_t size) {
    assert(address == 0x10); ++transactions;
    for (size_t n = 0; n < size; ++n) memory[reg + n] = data[n];
    return true;
  }
  bool probe_device(uint8_t address) { assert(address == 0x60); return false; }
};
struct DynamicProvider {
  std::vector<char> calls;
  unsigned clock_calls = 0, amplifier_calls = 0, led_calls = 0;
  int variant() { calls.push_back('V'); return 2; }
  int battery_report_lock() { calls.push_back('L'); return 1; }
  void battery_report_unlock() { calls.push_back('U'); }
  uint32_t reset_guard() { calls.push_back('G'); return 0; }
  uint32_t tick() { calls.push_back('T'); return 123 + clock_calls++; }
  bool disable_critical() { calls.push_back('D'); return false; }
  bool amplifier() { calls.push_back('A'); return amplifier_calls++ == 1; }
  bool led_wait() { calls.push_back('E'); ++led_calls; return false; }
};
// Both physical inputs are present and the receiver reports its stock ready bits.
// These assertions exercise recovered source and charge cores, not display text.
struct DualSourceIO : TaskIO {
  int receiver = 0xc0;
  std::vector<unsigned> labels;
  int find_receiver() { return receiver; }
  void source_changed(unsigned label) { labels.push_back(label); }
};
void test_simultaneous_sources() {
  for (bool remove_wireless : {false, true}) {
    SourceState source; ThermalHistory history; ChargeState charge;
    DualSourceIO source_io; Transport charge_io;
    source_io.wireless = source_io.wired = 1;
    auto step = [&]() {
      source_step(source, history, charge, source_io);
      charge_step(charge, {source.flags, 0, history.average}, charge_io);
    };
    step();
    assert(source.flags == 5 && source.external_charger == 1);
    assert(source.presentation == 4 && source_io.labels == std::vector<unsigned>{4});
    assert(charge.state == 1); // wireless branch wins from charge state0
    step(); assert(charge.state == 2);
    step(); assert(charge.state == 4);
    if (remove_wireless) {
      source_io.wireless = 0; source_io.receiver = -2;
      step();
      assert(source.flags == 1 && source.presentation == 2 && source.external_charger == 1);
      assert(source_io.labels.back() == 1);
      // Stock does not switch an active wireless charge state just because its
      // source flag vanished; enabled-pin state controls the next transition.
      assert(charge.state == 4);
      charge_io.enabled = 0; step(); assert(charge.state == 0);
      step(); assert(charge.state == 5);
    } else {
      source_io.wired = 0; step();
      assert(source.flags == 4 && source.presentation == 4 && source.external_charger == 1);
      assert(source_io.labels.back() == 4 && charge.state == 4);
    }
    source_io.wireless = source_io.wired = 0; source_io.receiver = -2;
    step(); assert(source.flags == 2 && source.external_charger == 0 && charge.state == 0);
    assert(source_io.labels.back() == 2);
  }
  // Presentation priority is not a command to restart an existing wired charge.
  SourceState source; ThermalHistory history; ChargeState charge;
  DualSourceIO source_io; Transport charge_io; source_io.wired = 1;
  for (unsigned i = 0; i < 3; ++i) {
    source_step(source, history, charge, source_io);
    charge_step(charge, {source.flags, 0, history.average}, charge_io);
  }
  assert(charge.state == 8 && source.flags == 1);
  source_io.wireless = 1;
  source_step(source, history, charge, source_io);
  charge_step(charge, {source.flags, 0, history.average}, charge_io);
  assert(source.flags == 5 && source.presentation == 4 && charge.state == 8);
}

struct BatteryReportProvider {
  int lock_result = 1;
  unsigned locks = 0, unlocks = 0;
  bool held = false;
  int battery_report_lock() {
    ++locks;
    held = lock_result == 1;
    return lock_result;
  }
  void battery_report_unlock() { assert(held); held = false; ++unlocks; }
  uint32_t reset_guard() { assert(held); return 0; }
  uint32_t tick() { assert(held); return 123; }
};
void test_battery_report_contract() {
  BatteryState battery;
  BatteryReportProvider provider;
  TaskIO io;
  for (int rejected : {0, 2}) {
    provider.lock_result = rejected;
    const auto index = battery.index;
    assert(!battery_sample_dynamic(battery, 3300, 2, provider, io));
    assert(battery.index == ((index + 1) & 63)); // history precedes the lock
    assert(!battery.updated && !battery.critical && battery.deadline == 0);
    assert(provider.unlocks == 0 && !provider.held);
  }
  provider.lock_result = 1;
  assert(battery_sample_dynamic(battery, 3300, 2, provider, io));
  assert(battery.critical && battery.updated && battery.deadline == 3600123);
  assert(provider.unlocks == 1 && !provider.held);
  // Sentinel paths release ownership too, without claiming an accepted report.
  for (uint16_t sentinel : {2952, 4232}) {
    assert(!battery_sample_dynamic(battery, sentinel, 1, provider, io));
    assert(battery.critical && battery.deadline == 3600123);
    assert(!provider.held);
  }
  assert(provider.locks == 5 && provider.unlocks == 3 && io.resets == 2);

  PowerController controller;
  PowerTaskInputs inputs;
  inputs.battery_report_lock_result = 0;
  auto result = controller.step(inputs, io, io, io, io);
  assert(result.sampled && !result.report_accepted);
  assert(controller.task_state().battery.initialized);
  inputs.battery_report_lock_result = 1;
  result = controller.step(inputs, io, io, io, io);
  assert(result.sampled && result.report_accepted);
}

void test_rolling_history_policy() {
  // Independent sums catch the stock next-slot bias after a complete ring cycle.
  auto sum_battery = [](const BatteryState &s) {
    uint32_t total = 0;
    for (auto sample : s.samples) total += sample;
    return total;
  };
  auto sum_thermal = [](const ThermalHistory &s) {
    uint32_t total = 0;
    for (auto sample : s.ring) total += sample;
    return total;
  };
  TaskIO io;
  BatteryState fixed, stock;
  fixed.index = stock.index = 42;
  battery_sample(fixed, 3400, 2, 1, 0, 0, io, HistoryPolicy::ROLLING_SUM);
  battery_sample(stock, 3400, 2, 1, 0, 0, io);
  for (unsigned visit = 0; visit < 128; ++visit) {
    battery_sample(fixed, 3330, 2, 1, 0, 0, io, HistoryPolicy::ROLLING_SUM);
    battery_sample(stock, 3330, 2, 1, 0, 0, io);
    assert(fixed.total == sum_battery(fixed));
  }
  assert(fixed.critical && !stock.critical);
  assert(fixed.total / 64 == 3330 && stock.total / 64 == 3331);
  for (uint16_t raw = 3300; raw < 4100; ++raw) {
    battery_sample(fixed, raw, 2, 1, 0, 0, io, HistoryPolicy::ROLLING_SUM);
    assert(fixed.total == sum_battery(fixed));
  }
  // Reseeding at a retained index still establishes the exact sum invariant.
  fixed.initialized = false;
  battery_sample(fixed, 3800, 2, 1, 0, 0, io, HistoryPolicy::ROLLING_SUM);
  assert(fixed.total == sum_battery(fixed) && fixed.total == 3800 * 64);

  ThermalHistory corrected, recovered;
  ChargeState shared;
  corrected.accept_pad(0, shared); recovered.accept_pad(0, shared);
  for (unsigned visit = 0; visit < 128; ++visit) {
    corrected.sample(80, HistoryPolicy::ROLLING_SUM); recovered.sample(80);
    assert(corrected.total == sum_thermal(corrected));
  }
  assert(corrected.average == 80 && recovered.average == 78);
  Transport charge_io;
  ChargeState corrected_charge{3, 0, 0}, stock_charge{3, 0, 0};
  charge_step(corrected_charge, {4, 0, corrected.average}, charge_io);
  charge_step(stock_charge, {4, 0, recovered.average}, charge_io);
  assert(corrected_charge.state == 2 && stock_charge.state == 4);
  for (unsigned raw = 0; raw <= 100; ++raw) {
    corrected.sample(raw, HistoryPolicy::ROLLING_SUM);
    assert(corrected.total == sum_thermal(corrected));
  }
  corrected.accept_pad(20, shared);
  corrected.sample(20, HistoryPolicy::ROLLING_SUM);
  assert(corrected.total == sum_thermal(corrected) && corrected.average == 20);

  // Composition forwards the explicit policy to both histories.
  PowerController controller;
  auto &state = controller.task_state();
  state.source.flags = state.source.previous_flags = 4;
  state.source.wireless_state = 1;
  state.thermal.accept_pad(0, shared);
  state.battery.initialized = state.source.flag_907c = true;
  state.battery.samples.fill(3400); state.battery.total = 3400 * 64;
  io.wireless = 1;
  controller.step({}, io, io, io, io, HistoryPolicy::ROLLING_SUM);
  assert(state.thermal.total == sum_thermal(state.thermal));
  assert(state.battery.total == sum_battery(state.battery));
}

void test_dynamic() {
  PowerController c;
  TaskIO io;
  DynamicProvider provider;
  c.task_state().shutdown.request = 1;
  auto result = c.step_dynamic(provider, io, io, io, io);
  assert(result.sampled);
  assert((provider.calls == std::vector<char>{'V','A','E','T','L','T','U','T','A','T'}));
  assert(provider.clock_calls == 4 && provider.led_calls == 1);
  assert(c.task_state().battery.deadline == 3600124);
  assert(c.task_state().shutdown.start_tick == 125);
}

void test_runtime() {
  RuntimeBus owner;
  HatchExpanderBus<RuntimeBus> bus(owner);
  TaskIO adc;
  RuntimeApplication app;
  PowerRuntime<decltype(bus), TaskIO, RuntimeApplication> runtime(bus, 0x10, adc, app, 0);
  runtime.initialize();
  assert(adc.init_calls.size() == 3 && app.init_calls.size() == 1);
  app.runtime_variant = 1;
  runtime.step();
  assert(owner.transactions == 0);
  app.runtime_variant = 2;
  auto result = runtime.step();
  assert(result.sampled && result.raw == 3600 && owner.transactions > 0);
  assert(runtime.controller().task_state().source.flags == 2);
  assert(runtime.controller().task_state().battery.reported == 30);
  runtime.cycle();
  assert(app.service_calls == 65);
  assert(runtime.controller().task_state().battery.index == 2);
}

void test_task() {
  PowerController controller;
  TaskIO io;
  auto &initial = controller.task_state().battery;
  initial.updated = 1; initial.deadline = 42;
  io.board_variant = 1;
  controller.initialize(io);
  assert(initial.updated == 1 && initial.deadline == 42 && io.init_calls.empty());
  io.board_variant = 2;
  controller.initialize(io);
  assert(initial.updated == 0 && initial.deadline == 0);
  assert((io.init_calls == std::vector<unsigned>{12, 1100, 3, 0x20}));
  PowerTaskInputs in;
  in.variant = 1;
  auto skipped = controller.step(in, io, io, io, io);
  assert(skipped.variant_skipped && !skipped.sampled && io.stages.empty());
  in.variant = 2;
  in.tick = 123;
  auto result = controller.step(in, io, io, io, io);
  auto &s = controller.task_state();
  assert(result.sampled && result.raw == 3600);
  assert(s.source.flags == 2 && s.charge.state == 0);
  assert(s.battery.initialized && s.source.flag_907c == 1);
  assert(s.battery.reported == 30 && s.battery.deadline == 3600123);
  assert((io.stages == std::vector<char>{'S','S','C','A','O','O','O'}));
  assert(!result.complete());
  // Publication accepts only after the repeated candidate reaches stock count3.
  s.status = {0, 0, 255, 99};
  s.source.presentation = 4;
  for (int visit = 0; visit < 3; ++visit) controller.step(in, io, io, io, io);
  assert(s.status.accepted == 0 && s.status.published == 99);
  controller.step(in, io, io, io, io);
  assert(s.status.accepted == 4 && s.status.published == 4);
  assert(io.stages[io.stages.size() - 2] == 'L' && io.stages.back() == 'U');

  // Source removal clears shared initialization BEFORE accepting the next raw.
  s.source.flags = s.source.previous_flags = 1;
  s.source.wired_state = 1;
  s.source.flag_907b = s.source.flag_907c = s.source.flag_907d = 1;
  io.calibrated = 2400; io.stages.clear();
  result = controller.step(in, io, io, io, io);
  assert(s.source.flags == 2 && result.raw == 4000);
  assert(s.battery.samples[20] == 4000 && s.battery.total == 4000 * 64);
  assert(s.source.flag_907b == 0 && s.source.flag_907d == 0);

  // A failed comparator scan skips reporting but preserves critical for shutdown.
  s.source.flags = s.source.previous_flags = 1;
  s.source.wired_state = 1; io.wired = 1; io.measurement = -1;
  s.battery.critical = true; s.battery.reported = 77;
  s.battery.comparator_index = 10;
  result = controller.step(in, io, io, io, io);
  assert(!result.sampled && s.battery.reported == 77 && s.battery.critical);
  assert(io.warnings == 1 && io.resets == 1);
  assert(s.battery.comparator_index == 255 && s.shutdown.comparator_index == 255);
  assert(s.source.flag_907b == 1 && s.battery.external_critical_latch == 1);
  controller.step(in, io, io, io, io);
  assert(io.warnings == 1); // same shared907b latch, not a separate shutdown copy
  // A failed source publication lock keeps app+194 stale. Shutdown must consume
  // that exact retained byte, not derive external presence from current flags.
  io.wired = 0; io.lock_result = 0; io.calibrated = 1980;
  controller.step(in, io, io, io, io);
  assert(s.source.flags == 2 && s.source.external_charger == 1);
  assert(s.shutdown.request == 0 && io.warnings == 2);
  io.lock_result = 1;
  controller.step(in, io, io, io, io);
  assert(s.source.external_charger == 0 && s.shutdown.request == 2);
}

int main() {
  test_battery_report_contract();
  test_rolling_history_policy();
  test_simultaneous_sources();
  test_dynamic();
  test_runtime();
  test_task();
  PowerController controller;
  const PowerController &view = controller;
  assert(view.state().state == 0);
  assert(view.state().flag_9080 == 0 && view.state().flag_907f == 0);
  assert(controller.status() == PowerControllerStatus::OFFLINE_TASK_INCOMPLETE);
  Transport io;
  auto result = controller.step_charge({1, 0, 60}, io);
  assert(!result.complete() && result.status == PowerControllerStatus::CHARGE_ONLY_INCOMPLETE);
  assert(!result.charge.transaction_failed && result.charge.chip_status == 2);
  assert(controller.state().state == 5);
  controller.step_charge({1, 0, 60}, io);
  assert(controller.state().state == 6);
  controller.step_charge({1, 0, 60}, io);
  assert(controller.state().state == 8);

  // Preserve stock unsigned byte comparisons exactly at each boundary.
  for (int temperature : {60, 61}) {
    controller.state() = {2, 0, 0}; io = {};
    controller.step_charge({4, 0, static_cast<uint8_t>(temperature)}, io);
    assert(controller.state().state == (temperature == 60 ? 4 : 2));
  }
  for (int temperature : {79, 80}) {
    for (uint8_t state : {3, 4}) {
      controller.state() = {state, 0, 0}; io = {};
      controller.step_charge({4, 0, static_cast<uint8_t>(temperature)}, io);
      assert(controller.state().state == (temperature == 79 ? 4 : 2));
      assert(controller.state().flag_9080 == (temperature == 80 && state == 3));
      assert(controller.state().flag_907f == (temperature == 80 && state == 4));
    }
  }
  // External thermal code would share these exact latch bytes. No recovery is
  // invented here: even a cold input does not reset already-latched flags.
  controller.state() = {2, 1, 1}; io = {};
  controller.step_charge({4, 0, 0}, io);
  assert(controller.state().state == 2);
  assert(view.state().flag_9080 == 1 && view.state().flag_907f == 1);
  controller.step_charge({2, 0, 0}, io);
  assert(controller.state().state == 0);
  assert(view.state().flag_9080 == 1 && view.state().flag_907f == 1);

  // A failed start keeps the numeric state but does not roll back prior writes.
  controller.state() = {5, 0, 0}; io = {}; io.fail_at = 2;
  result = controller.step_charge({1, 0, 60}, io);
  assert(result.charge.transaction_failed && controller.state().state == 5);
  assert(io.outputs == 0x20);
  assert((io.calls == std::vector<std::pair<char, int>>{
      {'r', 0x3a8}, {'c', 0x18}, {'c', 0x20}}));

  // Stock read failure is nonzero in state 2, but not success during start.
  controller.state() = {2, 0, 0}; io = {}; io.fail_at = 1;
  result = controller.step_charge({4, 0, 60}, io);
  assert(result.charge.transaction_failed && controller.state().state == 4);
  controller.state() = {5, 0, 0}; io = {}; io.fail_at = 4;
  result = controller.step_charge({1, 0, 60}, io);
  assert(result.charge.transaction_failed && controller.state().state == 5);

  controller.state() = {255, 0, 0}; io = {}; io.fail_at = 0;
  result = controller.step_charge({0, 0, 0}, io);
  assert(controller.state().state == 255 && result.charge.chip_status == 1);
  assert(result.charge.transaction_failed && io.calls.size() == 1);
}
