#include "hatch_status_led.h"

#include <driver/ledc.h>

#include "esphome/core/log.h"

namespace esphome::hatch_status_led {

static const char *const TAG = "hatch_status_led";

// Stock hal_check_riot_hardware (RIoT branch): low-speed group, timer 0, 12-bit, 171 Hz; ch0 GPIO23 (reference),
// ch1 GPIO13 (Wi-Fi), ch2 GPIO32 (lock). ESPHome's ledc outputs use the high-speed group, so this one is free.
static const ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t TIMER = LEDC_TIMER_0;
static const int PINS[3] = {23, 13, 32};
static const uint32_t HALF = 0x800, FULL = 0x1000;  // stock *0x40110d28 = 0x800
// Stock 0x40139c34: duty and hpoint per colour, against GPIO23 at 50 % / phase 0.
static const uint32_t DUTY[4] = {HALF, FULL, 0, HALF};
static const uint32_t HPOINT[4] = {0, 0, 0, HALF};
static const char *const NAMES[4] = {"off", "green", "red", "amber"};

void HatchStatusLed::loop() {
  // Configure once, on the first loop, after every component's setup().
  this->tried_ = true;
  this->ok_ = this->init_();
  if (this->ok_) {
    this->apply_(WINDOW_WIFI);
    this->apply_(WINDOW_LOCK);
  }
  this->disable_loop();
}

void HatchStatusLed::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch status LEDs (GPIO23 ref, GPIO13 Wi-Fi, GPIO32 lock)");
  if (this->tried_ && !this->ok_)
    ESP_LOGE(TAG, "  LEDC setup failed");
}

bool HatchStatusLed::init_() {
  ledc_timer_config_t timer{};
  timer.speed_mode = MODE;
  timer.duty_resolution = LEDC_TIMER_12_BIT;
  timer.timer_num = TIMER;
  timer.freq_hz = 171;
  timer.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t err = ledc_timer_config(&timer);
  for (int ch = 0; ch < 3 && err == ESP_OK; ch++) {
    ledc_channel_config_t conf{};
    conf.gpio_num = PINS[ch];
    conf.speed_mode = MODE;
    conf.channel = static_cast<ledc_channel_t>(ch);
    conf.timer_sel = TIMER;
    conf.duty = HALF;  // in phase with the reference = off
    conf.hpoint = 0;
    err = ledc_channel_config(&conf);
  }
  if (err != ESP_OK)
    ESP_LOGE(TAG, "LEDC setup failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

void HatchStatusLed::set(uint8_t window, uint8_t colour) {
  if (window > 1 || colour > 3)
    return;
  this->state_[window] = colour;
  if (this->ok_)
    this->apply_(window);  // otherwise applied once the LEDC is set up
}

void HatchStatusLed::set(uint8_t window, const std::string &colour) {
  for (uint8_t c = 0; c < 4; c++) {
    if (colour == NAMES[c]) {
      this->set(window, c);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown colour '%s'", colour.c_str());
}

void HatchStatusLed::apply_(uint8_t window) {
  ledc_channel_t ch = window == WINDOW_WIFI ? LEDC_CHANNEL_1 : LEDC_CHANNEL_2;
  uint8_t c = this->state_[window];
  ledc_set_duty_with_hpoint(MODE, ch, DUTY[c], HPOINT[c]);
  ledc_update_duty(MODE, ch);
  ESP_LOGD(TAG, "%s window: %s", window == WINDOW_WIFI ? "Wi-Fi" : "Lock", NAMES[c]);
}

}  // namespace esphome::hatch_status_led
