#pragma once
#include <cstdint>
#ifdef USE_ESP32
#include <driver/ledc.h>
#include <soc/ledc_struct.h>
#endif
namespace esphome::hatch_plus_power {
// ESPHome ledc_stop() retains the duty register. Its driven idle level is the
// physical output when signal generation is disabled.
template<class Hardware> bool applied_pwm_busy(uint8_t channel, Hardware &hardware) {
  const unsigned mode = channel < 8 ? 0 : 1;
  const unsigned index = channel % 8;
  return hardware.signal_enabled(mode, index) ? hardware.duty(mode, index) != 0
                                             : hardware.idle_high(mode, index);
}
#ifdef USE_ESP32
struct Esp32PwmHardware {
  bool signal_enabled(unsigned mode, unsigned channel) {
    return LEDC.channel_group[mode].channel[channel].conf0.sig_out_en;
  }
  bool idle_high(unsigned mode, unsigned channel) {
    return LEDC.channel_group[mode].channel[channel].conf0.idle_lv;
  }
  uint32_t duty(unsigned mode, unsigned channel) {
    return ledc_get_duty(static_cast<ledc_mode_t>(mode), static_cast<ledc_channel_t>(channel));
  }
};
inline bool pwm_output_busy(uint8_t channel) {
  Esp32PwmHardware hardware;
  return applied_pwm_busy(channel, hardware);
}
#endif
}  // namespace esphome::hatch_plus_power
