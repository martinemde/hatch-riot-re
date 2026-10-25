#pragma once

#include "esphome/core/component.h"
#include "esphome/components/hatch_expander/hatch_expander.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "power_status.h"
#include "runtime_adapter.h"
#include "adc_idf_adapter.h"
#include "greenpak_boot_transport.h"
#include "load_register_transport.h"
#include "load_controller.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/hatch_display/hatch_display.h"
#include <memory>

namespace esphome::hatch_plus_power {

// Diagnostics by default. Experimental local control requires explicit allow_control.
class HatchPlusPower : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void set_allow_control(bool value) { allow_control_ = value; }
  void set_light(light::LightState *value) { light_ = value; }
  void set_speaker(speaker::Speaker *value) { speaker_ = value; }
  void set_player(media_player::MediaPlayer *value) { player_ = value; }
  void set_pwm_channels(const std::vector<uint8_t> &channels) { pwm_channels_ = channels; }
  void set_display(hatch_display::HatchDisplay *value) { display_ = value; }
  int variant() { return 2; }
  int board() { return 1; }
  bool disable_critical() { return false; }
  uint32_t tick();
  void delay(unsigned ticks);
  int lock();
  void unlock() { app_locked_ = false; }
  bool amplifier() { return amplifier_enabled(load_); }
  bool led_wait() { return leds_enabled(load_); }
  void configure_virtual(unsigned bus, unsigned chip, unsigned reg, unsigned value);
  void source_changed(unsigned label);
  int reset_greenpak();
  void display(bool reset);
  void prepare();
  void state_shutdown(unsigned reason);
  void drain_logs();
  void external_critical();
  void service(uint32_t index, uint32_t delta);
  // Load-controller IO uses physical output latch transport, never UI state.
  bool device() { return loads_ != nullptr; }
  int write(unsigned pin, unsigned encoded) { return loads_->write(pin, encoded); }
  int read(unsigned pin, uint8_t &value) { return loads_->read(pin, value); }
  bool amp_lock() { if (amp_locked_) return false; amp_locked_ = true; return true; }
  void amp_unlock() { amp_locked_ = false; }
  int led_lock() { if (led_locked_) return 0; led_locked_ = true; return 1; }
  void led_unlock() { led_locked_ = false; }
  void update(unsigned group, bool ok);
  void timer_stop() { amp_timer_active_ = false; }
  bool timer_active() { return amp_timer_active_; }
  bool timer_start(uint32_t now) { amp_timer_tick_ = now; return amp_timer_active_ = true; }
  bool led_busy();
  void periodic();

  void update() override;
  void dump_config() override;
  void on_shutdown() override { this->stopped_ = true; }
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_expander(hatch_expander::HatchExpander *expander) { this->expander_ = expander; }
  void set_battery_percent(sensor::Sensor *sensor) { battery_percent_ = sensor; }
  void set_power_status(sensor::Sensor *sensor) { power_status_ = sensor; }
  void set_status_74(sensor::Sensor *sensor) { this->status_74_ = sensor; }
  void set_status_75(sensor::Sensor *sensor) { this->status_75_ = sensor; }
  void set_status_76(sensor::Sensor *sensor) { this->status_76_ = sensor; }
  void set_external_power_present(binary_sensor::BinarySensor *sensor) { this->external_power_ = sensor; }
  void set_wireless_voltage_present(binary_sensor::BinarySensor *sensor) { this->wireless_voltage_ = sensor; }
  void set_charge_enable_logic(binary_sensor::BinarySensor *sensor) { this->charge_enable_ = sensor; }

 protected:
  using Bus = HatchExpanderBus<hatch_expander::HatchExpander>;
  using Runtime = PowerRuntime<Bus, EspIdfAdc, HatchPlusPower>;
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<Runtime> runtime_;
  std::unique_ptr<GreenPakBootTransport<Bus, HatchPlusPower>> boot_;
  std::unique_ptr<LoadRegisterTransport<hatch_expander::HatchExpander>> loads_;
  EspIdfAdc adc_;
  LoadState load_;
  std::vector<uint8_t> pwm_channels_;
  light::LightState *light_{nullptr};
  media_player::MediaPlayer *player_{nullptr};
  speaker::Speaker *speaker_{nullptr};
  hatch_display::HatchDisplay *display_{nullptr};
  unsigned found_{2}, shutdown_reason_{0};
  uint32_t amp_timer_tick_{0}, led_second_tick_{0}, quiet_start_{0}, quiet_ticks_{0}, speaker_ready_tick_{0};
  bool speaker_ready_{false};
  bool allow_control_{false}, app_locked_{false}, amp_locked_{false}, led_locked_{false};
  bool amp_timer_active_{false}, init_failed_{false}, preparing_{false};
  bool desired_amp_{false}, desired_led_{false}, desired_initialized_{false};
  hatch_expander::HatchExpander *expander_{nullptr};
  sensor::Sensor *battery_percent_{nullptr};
  sensor::Sensor *power_status_{nullptr};
  sensor::Sensor *status_74_{nullptr};
  sensor::Sensor *status_75_{nullptr};
  sensor::Sensor *status_76_{nullptr};
  binary_sensor::BinarySensor *external_power_{nullptr};
  binary_sensor::BinarySensor *wireless_voltage_{nullptr};
  binary_sensor::BinarySensor *charge_enable_{nullptr};
  bool stopped_{false};
};
}  // namespace esphome::hatch_plus_power
