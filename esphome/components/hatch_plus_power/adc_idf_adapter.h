#pragma once
#include "adc_calibration.h"
#ifdef USE_ESP32
#include "driver/adc.h"
#include "esp_adc_cal.h"
namespace esphome::hatch_plus_power {
static_assert(ADC_UNIT_1 == 0 && ADC_WIDTH_BIT_12 == 12 && ADC_ATTEN_DB_12 == 3,
              "Stock ADC init values require the reviewed ESP-IDF 5.5 enum mapping");
// Native ESP-IDF 5.5 legacy ADC boundary. Construction performs no hardware IO.
// Only an explicitly initialized runtime may call raw/calibrate. No electrical
// calibration constants are synthesized: SDK characterization reads real eFuse.
class EspIdfAdc {
 public:
  int adc_width(unsigned width) {
    int result = adc1_config_width(static_cast<adc_bits_width_t>(width));
    width_ready_ = result == ESP_OK;
    return result;
  }
  unsigned characterize(unsigned unit, unsigned attenuation, unsigned width, unsigned default_vref) {
    auto source = esp_adc_cal_characterize(static_cast<adc_unit_t>(unit),
        static_cast<adc_atten_t>(attenuation),static_cast<adc_bits_width_t>(width),
        default_vref,&characteristics_);
    characterized_ = true;
    return static_cast<unsigned>(source);
  }
  int adc_attenuation(unsigned channel, unsigned attenuation) {
    int result = adc1_config_channel_atten(static_cast<adc1_channel_t>(channel),
                                          static_cast<adc_atten_t>(attenuation));
    attenuation_ready_ = result == ESP_OK;
    return result;
  }
  int raw(unsigned channel) { return adc1_get_raw(static_cast<adc1_channel_t>(channel)); }
  uint32_t calibrate(uint32_t raw_value) const {
    AdcCalibration c{static_cast<uint32_t>(characteristics_.adc_num),
        static_cast<uint32_t>(characteristics_.atten),
        static_cast<uint32_t>(characteristics_.bit_width), characteristics_.coeff_a,
        characteristics_.coeff_b, characteristics_.vref,
        characteristics_.low_curve, characteristics_.high_curve};
    return adc_raw_to_voltage(raw_value,c);
  }
  bool characterized() const { return characterized_; }
  bool ready() const { return width_ready_ && attenuation_ready_ && characterized_; }
 private:
  esp_adc_cal_characteristics_t characteristics_{};
  bool characterized_ = false;
  bool width_ready_ = false, attenuation_ready_ = false;
};
}  // namespace esphome::hatch_plus_power
#endif
