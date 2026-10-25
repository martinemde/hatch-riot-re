#include "hatch_plus_power.h"
#include "runtime_adapter.h"
#include "adc_idf_adapter.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "pwm_output_state.h"
#include "driver/ledc.h"
#include "soc/ledc_struct.h"

namespace esphome::hatch_plus_power {
static const char *const TAG = "hatch_plus_power";

void HatchPlusPower::setup() {
  if (!allow_control_) {
    ESP_LOGW(TAG, "DIAGNOSTICS ONLY: power control disabled");
    return;
  }
  if (!light_ || !player_ || !speaker_ || !display_ || pwm_channels_.size() != 4) {
    ESP_LOGE(TAG, "Power control requires light, player, speaker, display and four PWM channels");
    mark_failed(); return;
  }
  bus_ = std::make_unique<Bus>(*expander_);
  loads_ = std::make_unique<LoadRegisterTransport<hatch_expander::HatchExpander>>(*expander_, LoadExpander::AW9110B);
  boot_ = std::make_unique<GreenPakBootTransport<Bus,HatchPlusPower>>(*bus_, *this, found_, allow_control_);
  adc_.adc_width(12); adc_.characterize(0, 3, 12, 1100); adc_.adc_attenuation(3, 3);
  if (!adc_.ready()) ESP_LOGW(TAG, "Boot ADC configuration reported an error; continuing stock initialization");
  const int boot_raw = adc_.raw(3);
  if (boot_raw < 0) ESP_LOGW(TAG, "Boot ADC returned %d; retaining stock calibration input", boot_raw);
  greenpak_boot(allow_control_, adc_.calibrate(static_cast<uint32_t>(boot_raw)), *boot_);
  if (boot_->last_error()) ESP_LOGW(TAG, "GreenPAK boot reported %d; continuing stock initialization", boot_->last_error());
  if (found_ == 2) ESP_LOGW(TAG, "GreenPAK not found; register operations remain unavailable");
  runtime_ = std::make_unique<Runtime>(*bus_, greenpak_address(found_), adc_, *this, tick());
  runtime_->set_available(found_ != 2);
  runtime_->initialize();
  if (!adc_.ready()) ESP_LOGW(TAG, "ADC configuration reported an error; continuing stock power task");
  if (init_failed_) ESP_LOGW(TAG, "GreenPAK initialization write failed; continuing stock power task");
  // The recovered cold-boot table configures both power outputs low. Keep the
  // stock software cache defaults even when the bus reports a startup failure.
  if (loads_->initialize()) ESP_LOGW(TAG, "Power output initialization failed; continuing stock power task");
  load_.amp_initialized = 1;
  amplifier_request(load_, 0, *this);
  led_second_tick_ = tick();
  ESP_LOGW(TAG, "Experimental local power control initialized; no stock audio/UI parity claimed");
}

uint32_t HatchPlusPower::tick() { return uint32_t(esp_timer_get_time() / 1000); }
void HatchPlusPower::delay(unsigned ticks) {
  if (ticks == 10000) { quiet_start_ = tick(); quiet_ticks_ = ticks; }
  else vTaskDelay(pdMS_TO_TICKS(ticks) ? pdMS_TO_TICKS(ticks) : 1);
}
int HatchPlusPower::lock() { if (app_locked_) return 0; app_locked_ = true; return 1; }
void HatchPlusPower::configure_virtual(unsigned, unsigned, unsigned reg, unsigned value) {
  const uint8_t data = value;
  if (found_ == 2 || !bus_->write(greenpak_address(found_), reg, &data, 1)) init_failed_ = true;
}
void HatchPlusPower::source_changed(unsigned label) { ESP_LOGD(TAG, "Power source %u", label); }
int HatchPlusPower::reset_greenpak() {
  int result = boot_->reset();
  runtime_->set_address(greenpak_address(found_));
  runtime_->set_available(found_ != 2);
  return result;
}
void HatchPlusPower::display(bool reset) {
  if (reset) display_->show_text("rSt");
  else display_->show_text(std::to_string(runtime_->controller().task_state().battery.reported));
}
void HatchPlusPower::prepare() {
  if (!preparing_) {
    preparing_ = true;
    light_->turn_off().perform();
    player_->make_call().set_command(media_player::MEDIA_PLAYER_COMMAND_STOP).perform();
    speaker_->finish();
  }
  amplifier_request(load_, 0, *this);
  led_request(load_, 0, *this);
}
void HatchPlusPower::state_shutdown(unsigned reason) {
  shutdown_reason_ = reason;
  ESP_LOGW(TAG, "Local shutdown reason %u", reason);
  display_->clear();
}
void HatchPlusPower::drain_logs() { fflush(stdout); fflush(stderr); }
void HatchPlusPower::external_critical() { ESP_LOGW(TAG, "Critical battery on external charger"); }
void HatchPlusPower::service(uint32_t, uint32_t) {
  // Non-power stock slots are intentionally omitted. ESPHome schedules those
  // unrelated services itself; only the recovered power schedule is reproduced.
}
void HatchPlusPower::update(unsigned group, bool ok) {
  if (!ok) { status_set_warning(); ESP_LOGW(TAG, "Load group %u write failed", group); }
}
void HatchPlusPower::periodic() { if (preparing_ && !shutdown_reason_) display(false); }
bool HatchPlusPower::led_busy() {
  for (uint8_t channel : pwm_channels_)
    if (pwm_output_busy(channel)) return true;
  return false;
}

void HatchPlusPower::loop() {
  if (!allow_control_ || stopped_ || is_failed() || !runtime_) return;
  uint32_t now = tick();
  if (quiet_ticks_) {
    if (uint32_t(now - quiet_start_) < quiet_ticks_) return;
    quiet_ticks_ = 0;
  }
  if (amp_timer_active_ && uint32_t(now - amp_timer_tick_) >= 3000) {
    amp_timer_active_ = false; amplifier_timer(load_, *this);
  }
  if (uint32_t(now - led_second_tick_) >= 1000) {
    led_second_tick_ = now; led_second(load_, *this);
  }
  if (!preparing_) {
    const bool playing = player_->state == media_player::MEDIA_PLAYER_STATE_PLAYING ||
                         player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING;
    if (!speaker_->is_running()) speaker_ready_ = false;
    else if (!speaker_ready_) { speaker_ready_ = true; speaker_ready_tick_ = now; }
    const bool amp = playing && speaker_ready_ && uint32_t(now - speaker_ready_tick_) >= 30;
    const bool lit = light_->current_values.is_on();
    if (!desired_initialized_ || amp != desired_amp_ || bool(load_.amp_enabled) != amp) {
      if (amplifier_request(load_, amp, *this) == 0) desired_amp_ = amp;
    }
    if (!desired_initialized_ || lit != desired_led_ || bool(load_.led_enabled) != lit) {
      led_request(load_, lit, *this); desired_led_ = lit;
    }
    desired_initialized_ = true;
  }
  if (runtime_->poll()) {
    const auto &s = runtime_->controller().task_state();
    if (!s.shutdown.request && !shutdown_reason_) preparing_ = false;
    if (battery_percent_) battery_percent_->publish_state(s.battery.reported);
    if (power_status_) power_status_->publish_state(s.status.published);
    if (external_power_) external_power_->publish_state(s.source.external_charger != 0);

  }
}

void HatchPlusPower::dump_config() {
  ESP_LOGCONFIG(TAG, "RIoT+ power control: %s", allow_control_ ? "EXPERIMENTAL enabled" : "disabled (diagnostics only)");
  LOG_UPDATE_INTERVAL(this);
}

void HatchPlusPower::update() {
  if (this->stopped_ || this->allow_control_)
    return;
  uint8_t data[3];
  // Stock reads these RAM status bytes. Never probe blank-part address 0x08,
  // reset the GreenPAK, or change its virtual inputs or comparator reference.
  if (!this->expander_->read_device_registers(0x10, 0x74, data, sizeof(data))) {
    this->status_set_warning();
    if (this->status_74_ != nullptr)
      this->status_74_->publish_state(NAN);
    if (this->status_75_ != nullptr)
      this->status_75_->publish_state(NAN);
    if (this->status_76_ != nullptr)
      this->status_76_->publish_state(NAN);
    if (this->external_power_ != nullptr)
      this->external_power_->invalidate_state();
    if (this->wireless_voltage_ != nullptr)
      this->wireless_voltage_->invalidate_state();
    if (this->charge_enable_ != nullptr)
      this->charge_enable_->invalidate_state();
    return;
  }
  this->status_clear_warning();
  const PowerStatus status{data[0], data[1], data[2]};
  ESP_LOGD(TAG, "GreenPAK RAM 74=%02X 75=%02X 76=%02X (read-only)", data[0], data[1], data[2]);
  if (this->status_74_ != nullptr)
    this->status_74_->publish_state(data[0]);
  if (this->status_75_ != nullptr)
    this->status_75_->publish_state(data[1]);
  if (this->status_76_ != nullptr)
    this->status_76_->publish_state(data[2]);
  if (this->external_power_ != nullptr)
    this->external_power_->publish_state(status.external_power_present());
  if (this->wireless_voltage_ != nullptr)
    this->wireless_voltage_->publish_state(status.wireless_voltage_present());
  if (this->charge_enable_ != nullptr)
    this->charge_enable_->publish_state(status.charge_enable_logic());
}
}  // namespace esphome::hatch_plus_power
