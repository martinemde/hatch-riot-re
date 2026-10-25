#pragma once
#include "power_controller.h"
#include "register_transport.h"
#include "wireless_controller.h"
#include "scheduler_controller.h"

namespace esphome::hatch_plus_power {
// Owner is HatchExpander in a target build; templated for host transaction tests.
// No driver is created and no bus state is duplicated.
template<class Owner> class HatchExpanderBus {
 public:
  explicit HatchExpanderBus(Owner &owner) : owner_(owner) {}
  bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    return owner_.read_device_registers(address, reg, data, size);
  }
  bool write(uint8_t address, uint8_t reg, const uint8_t *data, size_t size) {
    return owner_.write_device_registers(address, reg, data, size);
  }
  bool probe(uint8_t address) { return owner_.probe_device(address); }
 private:
  Owner &owner_;
};

// Connected dependency adapter, not an automatically scheduled/live component.
// Bus is the sole bus owner's read/write/probe facade. ADC owns characterization.
// Application must implement every lifecycle/publication/load method used below;
// no successful reset/shutdown/publication is fabricated when a dependency is absent.
template<class Bus, class ADC, class Application>
class PowerRuntime : public ShutdownTransport {
 public:
  PowerRuntime(Bus &bus, uint8_t greenpak_address, ADC &adc, Application &app, uint32_t initial_tick)
      : greenpak_(bus, greenpak_address),
        wireless_(bus), adc_(adc), app_(app), scheduler_(initial_tick) {}
  PowerController &controller() { return controller_; }
  const PowerController &controller() const { return controller_; }
  void initialize() { controller_.initialize(*this); }
  void set_address(uint8_t address) { greenpak_.set_address(address); }
  void set_available(bool available) { greenpak_.set_available(available); }
  // All other stock service slots remain mandatory application dependencies.
  // Construction starts neither hardware IO nor a background scheduler.
  void cycle() { scheduler_.cycle(*this); }
  bool poll() { return scheduler_.poll(*this); }
  uint32_t tick() { return app_.tick(); }
  void service(uint32_t index, uint32_t delta) {
    if (index == 3) step();
    else app_.service(index, delta);
  }
  // Required application provider methods are sampled at the stock stage sites:
  // variant, amplifier, led_wait (physical cache), tick, board,
  // disable_critical. No task-wide early snapshot is used by the runtime path.
  PowerControllerResult step() {
    return controller_.step_dynamic(app_, *this, *this, *this, *this);
  }
  int variant() { return app_.variant(); }
  void adc_width(unsigned bits) { adc_.adc_width(bits); }
  void characterize(unsigned unit, unsigned attenuation, unsigned bits, unsigned vref) {
    adc_.characterize(unit, attenuation, bits, vref);
  }
  void adc_attenuation(unsigned channel, unsigned attenuation) {
    adc_.adc_attenuation(channel, attenuation);
  }
  void configure_virtual(unsigned bus, unsigned chip, unsigned reg, unsigned value) {
    app_.configure_virtual(bus, chip, reg, value);
  }
  int read(uint16_t bit) { return greenpak_.read(bit); }
  int read_pin(uint16_t bit) { return greenpak_.read_pin(bit); }
  int read() { return greenpak_.read(); }
  bool set(uint8_t mask) { return greenpak_.set(mask); }
  bool clear(uint8_t mask) { return greenpak_.clear(mask); }
  bool reference(uint8_t setting) { return greenpak_.reference(setting); }
  uint32_t raw(unsigned channel) { return adc_.raw(channel); }
  uint32_t calibrate(uint32_t value) { return adc_.calibrate(value); }
  uint32_t inactive_adc_raw(unsigned channel) { return raw(channel); }
  uint32_t inactive_adc_to_voltage(uint32_t value) { return calibrate(value); }
  int find_receiver() { return wireless_.find_receiver(); }
  int receiver_status() { return wireless_.receiver_status(); }
  int temperature() { return wireless_.temperature(); }
  int clear_receiver_bits(uint8_t reg, uint8_t mask) { return wireless_.clear_receiver_bits(reg, mask); }
  int set_receiver_voltage(double volts) { return wireless_.set_receiver_voltage(volts); }
  int app_lock() { return app_.lock(); }
  void app_unlock() { app_.unlock(); }
  bool lock() { return app_.lock() == 1; }
  void unlock() { app_.unlock(); }
  void source_changed(unsigned label) { app_.source_changed(label); }
  bool reset() { return app_.reset_greenpak() == 0; }
  int read(unsigned bit, uint8_t &value) override {
    int result = greenpak_.read(bit);
    if (result < 0) return 1;
    value = result; return 0;
  }
  void display(bool reset) override { app_.display(reset); }
  void prepare() override { app_.prepare(); }
  void state_shutdown(unsigned reason) override { app_.state_shutdown(reason); }
  void drain_logs() override { app_.drain_logs(); }
  int set_virtual(unsigned mask) override { return greenpak_.set(mask) ? 0 : 1; }
  void delay(unsigned ticks) override { app_.delay(ticks); }
  void external_critical() override { app_.external_critical(); }
  int reset_greenpak() override { return app_.reset_greenpak(); }
 private:
  GreenPakRegisterTransport<Bus> greenpak_;
  WirelessController<Bus> wireless_;
  ADC &adc_;
  Application &app_;
  PowerController controller_{};
  SchedulerCursor scheduler_;
};
}  // namespace esphome::hatch_plus_power
