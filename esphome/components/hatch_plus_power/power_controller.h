#pragma once
#include "source_controller.h"
#include "adc_controller.h"
#include "battery_controller.h"
#include "shutdown_controller.h"
#include "init_controller.h"
#include "status_controller.h"

namespace esphome::hatch_plus_power {
// Offline task composition only. No bus adapter, scheduler or live YAML wiring.
// Publication locking and status debounce are composed here. Shutdown application
// lifecycle remains an injected boundary; this is not a complete live adapter.
enum class PowerControllerStatus { CHARGE_ONLY_INCOMPLETE, OFFLINE_TASK_INCOMPLETE };
struct PowerControllerResult {
  ChargeResult charge{};
  PowerControllerStatus status = PowerControllerStatus::OFFLINE_TASK_INCOMPLETE;
  SourceResult source{};
  bool sampled = false;
  bool report_accepted = false;
  uint16_t raw = 0;
  bool variant_skipped = false;
  constexpr bool complete() const { return false; }
};
struct PowerTaskInputs {
  uint8_t mode = 0;
  int battery_report_lock_result = 1;
  uint32_t reset_guard = 0, tick = 0;
  bool disable_critical = false;
  bool amplifier = false, led_wait = false;
  int variant = 2;
};
struct PowerTaskState {
  ChargeState charge{};
  SourceState source{};
  ThermalHistory thermal{};
  BatteryState battery{};
  ShutdownState shutdown{};
  StatusState status{};
};
class PowerController {
 public:
  ChargeState &state() { return task_.charge; }
  const ChargeState &state() const { return task_.charge; }
  PowerTaskState &task_state() { return task_; }
  const PowerTaskState &task_state() const { return task_; }
  constexpr PowerControllerStatus status() const {
    return PowerControllerStatus::OFFLINE_TASK_INCOMPLETE;
  }
  template<class IO> void initialize(IO &io) {
    InitState init{task_.battery.updated, task_.battery.deadline};
    power_init(init, io);
    task_.battery.updated = init.flag_9084;
    task_.battery.deadline = init.timer_9088;
  }
  template<class Transport>
  PowerControllerResult step_charge(ChargeInputs inputs, Transport &io) {
    PowerControllerResult result;
    result.charge = charge_step(task_.charge, inputs, io);
    result.status = PowerControllerStatus::CHARGE_ONLY_INCOMPLETE;
    return result;
  }
  // Stage-specific adapters keep stock error conventions distinct: charge/source
  // bool writes, comparator bool reference/reset, shutdown zero-success writes.
  // source owns shared907b/c/d between calls. Battery owns comparator and critical.
  struct SnapshotProvider {
    const PowerTaskInputs &in;
    unsigned load_calls = 0;
    int variant() { return in.variant; }
    int battery_report_lock() { return in.battery_report_lock_result; }
    void battery_report_unlock() {}
    uint32_t tick() { return in.tick; }
    bool disable_critical() { return in.disable_critical; }
    bool amplifier() { return load_calls++ == 0 ? (in.mode & 1) != 0 : in.amplifier; }
    bool led_wait() { return load_calls++ == 1 ? (in.mode & 2) != 0 : in.led_wait; }
  };
  template<class SourceIO, class ChargeIO, class AcquisitionIO, class ShutdownIO>
  PowerControllerResult step(const PowerTaskInputs &in, SourceIO &source_io, ChargeIO &charge_io,
                             AcquisitionIO &acquisition_io, ShutdownIO &shutdown_io,
                             HistoryPolicy history_policy = HistoryPolicy::STOCK_NEXT_SLOT) {
    SnapshotProvider provider{in};
    return step_dynamic(provider, source_io, charge_io, acquisition_io, shutdown_io, history_policy);
  }
  template<class Provider, class SourceIO, class ChargeIO, class AcquisitionIO, class ShutdownIO>
  PowerControllerResult step_dynamic(Provider &in, SourceIO &source_io,
                             ChargeIO &charge_io, AcquisitionIO &acquisition_io,
                             ShutdownIO &shutdown_io,
                             HistoryPolicy history_policy = HistoryPolicy::STOCK_NEXT_SLOT) {
    if (in.variant() != 2) {
      PowerControllerResult result;
      result.variant_skipped = true;
      return result;
    }
    auto &s = task_.source;
    auto &b = task_.battery;
    auto &off = task_.shutdown;
    PowerControllerResult result;
    result.source = source_step(s, task_.thermal, task_.charge, source_io, history_policy);
    result.charge = charge_step_dynamic(task_.charge, {s.flags, 0, task_.thermal.average}, charge_io, [&]() {
      const uint8_t amplifier = in.amplifier();
      return uint8_t(amplifier | (uint8_t(in.led_wait()) << 1));
    });
    b.initialized = s.flag_907c;
    b.external_critical_latch = s.flag_907b;
    result.sampled = adc_sample(s.flags, b.comparator_index, b.updated, b.deadline,
                                in.tick(), acquisition_io, result.raw);
    if (!result.sampled)
      result.sampled = battery_measure(b, acquisition_io, s.flag_907d != 0, result.raw);
    struct BatteryProvider {
      Provider &provider;
      SourceState &source;
      int battery_report_lock() { return provider.battery_report_lock(); }
      void battery_report_unlock() { provider.battery_report_unlock(); }
      uint32_t reset_guard() { return source.inactive_count; }
      uint32_t tick() { return provider.tick(); }
    } battery_provider{in, s};
    if (result.sampled)
      result.report_accepted = battery_sample_dynamic(b, result.raw, s.flags, battery_provider, acquisition_io, history_policy);
    s.flag_907c = b.initialized;
    off.external_warning = b.external_critical_latch;
    off.comparator_index = b.comparator_index;
    const ShutdownInput shutdown_input{s.flags, b.critical, s.external_charger != 0,
                                       false, 0, false, false};
    const int enabled = shutdown_buttons(off, shutdown_input, shutdown_io);
    status_update(task_.status, s.presentation, enabled, shutdown_io);
    shutdown_critical_dynamic(off, shutdown_input, shutdown_io, in);
    b.external_critical_latch = s.flag_907b = off.external_warning;
    b.comparator_index = off.comparator_index;
    return result;
  }
 private:
  PowerTaskState task_{};
};
}  // namespace esphome::hatch_plus_power
