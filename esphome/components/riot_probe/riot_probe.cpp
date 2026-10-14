#include "riot_probe.h"

#include <cinttypes>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <sdmmc_cmd.h>
#include <strings.h>

#include <cstring>
#include <memory>

#include "diskio_impl.h"
#include "ff.h"

#include "esphome/components/audio/audio_decoder.h"
#include "esphome/components/ring_buffer/ring_buffer.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace riot_probe {

static const char *const TAG = "riot_probe";

// Only this address may ever be written, and only these registers.
static const uint8_t EXPANDER_ADDR = 0x58;
static const uint8_t ACCEL_ADDR = 0x27;
static const uint32_t STATUS_HALF = 0x800, STATUS_FULL = 0x1000;  // 12-bit LEDC duty (stock *0x40110d28 = 0x800)  // DA213 on this unit (stock tries 0x26, 0x27, 0x62)
static const uint8_t AW9523B_ID = 0x23;
static const uint8_t REG_IN0 = 0x00, REG_IN1 = 0x01, REG_OUT0 = 0x02, REG_OUT1 = 0x03, REG_CFG0 = 0x04,
                     REG_CFG1 = 0x05, REG_ID = 0x10, REG_GCR = 0x11;
static const uint8_t GCR_P0_PUSH_PULL = 0x10;
// The stock firmware drives this chip with its AW9110B driver (10 pins; pin table 0x3f4a8c18): logical 0-3 =
// P1_0-P1_3 (LED/SD/amp/ID_OUT enables), 4-8 = P0_0-P0_4 (buttons, inputs), 9 = P0_5 (touch enable). Its config
// is read-modify-write, so the other six bits keep their power-on state (output, low) forever. Releasing P0_6
// wedged the bus on hardware, so the probe never changes those bits either.
static const uint16_t NEVER_DRIVE = 0xF0C0;  // P0_6, P0_7, P1_4-P1_7
// The stock config makes exactly the five button pins inputs.
static const uint8_t CFG0_BUTTONS = 0x1F;
// Quiet time after an expander write (a load switching on NACKed the next write ~3 ms later), gap between
// retries, and attempts per write.
static const uint32_t SETTLE_MS = 20, RETRY_GAP_MS = 10;
static const int WRITE_ATTEMPTS = 3;

// Stock font, ASCII 0x20..0x7E (firmware table at 0x3f486910).
static const uint8_t FONT[95] = {
    0x00, 0x86, 0x22, 0x7e, 0x6d, 0xd2, 0x46, 0x20, 0x29, 0x0b, 0x21, 0x70, 0x10, 0x40, 0x80, 0x52,  // ' '../
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x09, 0x0d, 0x61, 0x48, 0x43, 0xd3,  // 0..?
    0x5f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, 0x3d, 0x76, 0x30, 0x1e, 0x75, 0x38, 0x15, 0x37, 0x3f,  // @..O
    0x73, 0x6b, 0x33, 0x6d, 0x78, 0x3e, 0x3e, 0x2a, 0x76, 0x6e, 0x5b, 0x39, 0x64, 0x0f, 0x23, 0x08,  // P.._
    0x02, 0x5f, 0x7c, 0x58, 0x5e, 0x7b, 0x71, 0x6f, 0x74, 0x10, 0x0c, 0x75, 0x30, 0x14, 0x54, 0x5c,  // `..o
    0x73, 0x67, 0x50, 0x6d, 0x78, 0x1c, 0x1c, 0x14, 0x76, 0x6e, 0x5b, 0x46, 0x30, 0x70, 0x01,        // p..~
};

// Stock brightness table (0x3f432e0c), index = level 0..15.
static const uint8_t BRIGHTNESS[16] = {0xfe, 0x7e, 0xbe, 0x3e, 0xde, 0x5e, 0x9e, 0x1e,
                                       0xee, 0x6e, 0xae, 0x2e, 0xce, 0x4e, 0x8e, 0x0e};

uint8_t seg_encode(char c) {
  if (c < 0x20 || c > 0x7e)
    return 0x00;
  return FONT[c - 0x20];
}

uint8_t seg_brightness(uint8_t level) { return BRIGHTNESS[level > 15 ? 15 : level]; }

void RiotProbe::setup() {
  this->detect_variant_();
  this->bus_.init(this->sda_pin_, this->scl_pin_, this->frequency_);
  if (!this->bus_.clear())
    ESP_LOGW(TAG, "I2C bus not idle at boot (SDA=%d SCL=%d) after clearing", this->bus_.sda_high(),
             this->bus_.scl_high());
  // The scan the i2c: bus used to do: expect 0x58 (expander), 0x27 (accel), maybe 0x70 (display).
  char found[64] = {0};
  size_t pos = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (this->bus_.probe(a) && pos < sizeof(found) - 6)
      pos += snprintf(found + pos, sizeof(found) - pos, " 0x%02X", a);
    delay(1);
  }
  ESP_LOGI(TAG, "I2C scan (bit-banged):%s", pos ? found : " nothing answered");
  // Wait until every bus is up before the first I2C access.
  this->ready_at_ = millis() + 2000;
  this->set_interval("accel", 1000, [this]() {
    if (this->accel_ready_)
      this->accel_read_();
  });
  // Polling read failures are otherwise silent; log expander I2C error counts so the bus health is visible.
  this->set_interval("i2c_stats", 30000, [this]() { this->log_i2c_stats_(); });
}

void RiotProbe::count_(bool write, i2c::ErrorCode err) {
  (write ? this->win_writes_ : this->win_reads_)++;
  if (err != i2c::ERROR_OK)
    this->win_errors_[err < ERR_SLOTS ? err : ERR_SLOTS - 1]++;
}

void RiotProbe::log_i2c_stats_() {
  uint32_t errors = 0;
  for (int i = 0; i < ERR_SLOTS; i++) {
    errors += this->win_errors_[i];
    this->total_errors_ += this->win_errors_[i];
  }
  this->total_ops_ += this->win_reads_ + this->win_writes_;
  // Codes: 2 = NACK, 3 = timeout, 6 = unknown (see i2c::ErrorCode).
  ESP_LOGI(TAG,
           "I2C stats (30 s): %" PRIu32 " reads, %" PRIu32 " writes, %" PRIu32 " errors [nack=%" PRIu32
           " timeout=%" PRIu32 " unknown=%" PRIu32 " other=%" PRIu32 "]; since boot %" PRIu32 "/%" PRIu32
           " failed. Pins at boot: %s",
           this->win_reads_, this->win_writes_, errors, this->win_errors_[i2c::ERROR_NOT_ACKNOWLEDGED],
           this->win_errors_[i2c::ERROR_TIMEOUT], this->win_errors_[i2c::ERROR_UNKNOWN],
           errors - this->win_errors_[i2c::ERROR_NOT_ACKNOWLEDGED] - this->win_errors_[i2c::ERROR_TIMEOUT] -
               this->win_errors_[i2c::ERROR_UNKNOWN],
           this->total_errors_, this->total_ops_, this->buttons_at_boot_ ? "buttons INPUT" : "power-on default");
  this->win_reads_ = this->win_writes_ = 0;
  for (int i = 0; i < ERR_SLOTS; i++)
    this->win_errors_[i] = 0;
}

void RiotProbe::detect_variant_() {
  // Same as stock hal_riot_check_i2c_pins (0x401391d8): inputs with pull-downs, then reset the pins.
  gpio_config_t cfg{};
  cfg.pin_bit_mask = (1ULL << 13) | (1ULL << 32) | (1ULL << 23);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  delay(100);
  this->sense_[0] = gpio_get_level(GPIO_NUM_13);
  this->sense_[1] = gpio_get_level(GPIO_NUM_32);
  this->sense_[2] = gpio_get_level(GPIO_NUM_23);
  gpio_reset_pin(GPIO_NUM_13);
  gpio_reset_pin(GPIO_NUM_32);
  gpio_reset_pin(GPIO_NUM_23);

  if (!this->sense_[0] && !this->sense_[1] && !this->sense_[2]) {
    this->variant_ = "RIoT";
  } else if (this->sense_[0] && this->sense_[1] && this->sense_[2]) {
    this->variant_ = "RIoT+";
  } else {
    this->variant_ = "ambiguous";
  }
  ESP_LOGI(TAG, "Variant sense GPIO13=%u GPIO32=%u GPIO23=%u -> %s", this->sense_[0], this->sense_[1],
           this->sense_[2], this->variant_.c_str());
}

void RiotProbe::settle_() {
  // An output change can switch a load (LED driver, touch controller...), and for a few ms afterwards the
  // expander NACKs and transactions risk hanging the bus. Keep every expander transaction out of that window.
  int32_t wait = int32_t(this->quiet_until_ - millis());
  if (wait > 0)
    delay(wait);
}

bool RiotProbe::read_reg_(uint8_t reg, uint8_t *value) {
  this->settle_();
  i2c::ErrorCode err = this->read_register(reg, value, 1);
  this->count_(false, err);
  return err == i2c::ERROR_OK;
}

bool RiotProbe::write_reg_(uint8_t reg, uint8_t value) {
  // Register writes are idempotent, so retrying is safe. Space attempts out instead of retrying straight back
  // into the post-switch window, and leave a quiet period after every write (each may change an output).
  for (int attempt = 1; attempt <= WRITE_ATTEMPTS; attempt++) {
    this->settle_();
    i2c::ErrorCode err = this->write_register(reg, &value, 1);
    this->count_(true, err);
    this->quiet_until_ = millis() + (err == i2c::ERROR_OK ? SETTLE_MS : RETRY_GAP_MS);
    if (err == i2c::ERROR_OK)
      return true;
    ESP_LOGW(TAG, "Write expander reg 0x%02X <- 0x%02X failed (attempt %d/%d, i2c error %d)", reg, value, attempt,
             WRITE_ATTEMPTS, (int) err);
  }
  return false;
}

bool RiotProbe::snapshot_() {
  this->snapshot_tried_ = true;
  if (!this->read_reg_(REG_ID, &this->chip_id_)) {
    ESP_LOGW(TAG, "No response from GPIO expander at 0x%02X", EXPANDER_ADDR);
    return false;
  }
  bool ok = this->read_reg_(REG_OUT0, &this->snap_out_[0]) && this->read_reg_(REG_OUT1, &this->snap_out_[1]) &&
            this->read_reg_(REG_CFG0, &this->snap_cfg_[0]) && this->read_reg_(REG_CFG1, &this->snap_cfg_[1]) &&
            this->read_reg_(REG_GCR, &this->snap_gcr_);
  if (!ok) {
    ESP_LOGW(TAG, "Expander snapshot failed");
    return false;
  }
  ESP_LOGI(TAG, "Expander ID=0x%02X (%s). Boot snapshot: OUT=%02X %02X CFG=%02X %02X GCR=%02X", this->chip_id_,
           this->chip_id_ == AW9523B_ID ? "AW9523B" : "NOT AW9523B, writes disabled", this->snap_out_[1],
           this->snap_out_[0], this->snap_cfg_[1], this->snap_cfg_[0], this->snap_gcr_);
  this->dump_expander();
  if (this->buttons_at_boot_ && this->chip_id_ == AW9523B_ID) {
    // Match the stock boot config for the button pins (P0_0-P0_4 inputs) and make it the restore() baseline.
    uint8_t cfg0 = this->snap_cfg_[0] | CFG0_BUTTONS;
    if (this->write_reg_(REG_CFG0, cfg0)) {
      this->snap_cfg_[0] = cfg0;
      ESP_LOGW(TAG, "buttons_at_boot: P0_0-P0_4 set to INPUT (CFG0=%02X, new restore baseline)", cfg0);
    } else {
      ESP_LOGE(TAG, "buttons_at_boot: could not set button pins to INPUT");
    }
  }
  return true;
}

bool RiotProbe::read_inputs_(uint16_t *value) {
  uint8_t p0, p1;
  if (!this->read_reg_(REG_IN0, &p0) || !this->read_reg_(REG_IN1, &p1))
    return false;
  *value = (uint16_t(p1) << 8) | p0;
  return true;
}

void RiotProbe::loop() {
  uint32_t now = millis();
  if (now < this->ready_at_)
    return;

  // Back off geometrically on consecutive I2C failures. A wedged bus (SCL held low after a brownout) makes
  // every read block for the full timeout; polling that every 50 ms pins the CPU and starves the web server /
  // API, which looks like the device going offline. Backing off keeps the device responsive and lets it
  // recover on its own once the bus clears.
  uint32_t interval = this->poll_interval_;
  if (this->fail_streak_ > 2) {
    uint32_t shift = this->fail_streak_ - 2;
    if (shift > 6)
      shift = 6;
    interval = this->poll_interval_ << shift;  // 50 ms → up to ~3.2 s, clamped to 2 s
    if (interval > 2000)
      interval = 2000;
  }
  // Like stock, read the expander when its INT line (GPIO34, active low, cleared by reading the inputs) says
  // something changed, instead of polling continuously. Every I2C transaction is a chance to hit a bus glitch
  // (LED PWM current wedged the bus on hardware), so idle reads drop to a slow fallback. Keep the fast rate while
  // a debounce candidate is pending or before the first baseline read.
  bool pending = !this->snapshot_ok_ || !this->have_inputs_ || this->candidate_streak_ > 0;
  if (!pending && gpio_get_level(GPIO_NUM_34) != 0 && interval < this->fallback_interval_)
    interval = this->fallback_interval_;
  if (now - this->last_poll_ < interval)
    return;
  this->last_poll_ = now;

  // Keep retrying the boot snapshot (throttled by the back-off above) until the bus answers, so a boot with a
  // briefly-wedged bus still comes good once it clears, instead of disabling the probe for the whole session.
  if (!this->snapshot_ok_) {
    this->snapshot_ok_ = this->snapshot_();
    if (!this->snapshot_ok_) {
      this->fail_streak_++;
      return;
    }
    this->fail_streak_ = 0;
    return;
  }

  uint16_t value;
  if (!this->read_inputs_(&value)) {
    this->fail_streak_++;
    return;
  }
  this->fail_streak_ = 0;
  if (!this->have_inputs_) {
    this->have_inputs_ = true;
    this->inputs_ = value;
    this->change_count_++;
    ESP_LOGI(TAG, "Expander inputs initial P1:P0 = %02X %02X", value >> 8, value & 0xFF);
    return;
  }
  uint16_t changed = value ^ this->inputs_;
  if (changed == 0) {
    this->candidate_streak_ = 0;  // reading matches accepted state; clear any pending candidate
    return;
  }
  // Debounce against one-off bad reads (the wedged bus occasionally returns 0xFF for a whole port, which would
  // otherwise poison seen_toggling and block legitimate pin drives). Require the new value to repeat on two
  // consecutive polls before accepting it. Real button edges last far longer than one 50 ms poll.
  if (value != this->candidate_value_) {
    this->candidate_value_ = value;
    this->candidate_streak_ = 1;
    return;
  }
  if (++this->candidate_streak_ < 2)
    return;
  this->candidate_streak_ = 0;
  // Ignore changes caused by our own driving of a pin.
  uint16_t external = changed & ~this->driven_mask_;
  this->seen_toggling_ |= external;
  this->inputs_ = value;
  this->change_count_++;
  ESP_LOGI(TAG, "Expander inputs P1:P0 = %02X %02X  changed=%04X  seen_toggling=%04X", value >> 8, value & 0xFF,
           changed, this->seen_toggling_);
  for (uint8_t bit = 0; bit < 16; bit++) {
    if (changed & (1u << bit))
      ESP_LOGI(TAG, "  P%u_%u -> %u", bit / 8, bit % 8, (value >> bit) & 1);
  }
}

void RiotProbe::dump(uint8_t address, uint8_t start, uint8_t count) {
  uint8_t saved = this->address_;
  this->set_i2c_address(address);
  ESP_LOGI(TAG, "Register dump 0x%02X [0x%02X..0x%02X] (read only):", address, start, uint8_t(start + count - 1));
  char line[3 * 16 + 1];
  for (uint16_t base = start; base < uint16_t(start) + count; base += 16) {
    size_t pos = 0;
    for (uint16_t reg = base; reg < base + 16 && reg < uint16_t(start) + count; reg++) {
      uint8_t v;
      delay(5);  // spacing test: back-to-back reads failed ~50 %
      if (this->read_register(uint8_t(reg), &v, 1) == i2c::ERROR_OK) {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", v);
      } else {
        pos += snprintf(line + pos, sizeof(line) - pos, "-- ");
      }
    }
    ESP_LOGI(TAG, "  %02X: %s", base, line);
  }
  this->set_i2c_address(saved);
}

void RiotProbe::accel_setup() {
  // The stock DA213 path (accelerometer_init 0x400ff590 + accelerometer_da213_setup 0x400ffac0), used when the
  // part answers at 0x27: soft reset, then RANGE/ODR/POWER = 00 03 06. These are the only accelerometer writes.
  uint8_t saved = this->address_;
  this->set_i2c_address(ACCEL_ADDR);
  this->settle_();
  uint8_t reset = 0x24;
  i2c::ErrorCode e1 = this->write_register(0x00, &reset, 1);
  delay(10);
  static const uint8_t CFG[3] = {0x00, 0x03, 0x06};
  i2c::ErrorCode e2 = this->write_register(0x0F, CFG, 3);
  this->set_i2c_address(saved);
  this->accel_ready_ = e1 == i2c::ERROR_OK && e2 == i2c::ERROR_OK;
  ESP_LOGW(TAG, "Accel 0x27: stock DA213 setup (reset, 0x0F-0x11 = 00 03 06): %s (i2c %d/%d)",
           this->accel_ready_ ? "ok" : "FAILED", (int) e1, (int) e2);
}

void RiotProbe::accel_read_() {
  uint8_t saved = this->address_;
  this->set_i2c_address(ACCEL_ADDR);
  this->settle_();
  uint8_t raw[6];
  i2c::ErrorCode err = this->read_register(0x02, raw, sizeof(raw));
  this->set_i2c_address(saved);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Accel read failed (i2c %d)", (int) err);
    return;
  }
  // Stock (0x400ffa50): little-endian int16 per axis, divided by 4 → 14-bit counts; 4096 counts = 1 g at +-2 g.
  for (int i = 0; i < 3; i++)
    this->accel_[i] = int16_t(raw[2 * i] | (raw[2 * i + 1] << 8)) / 4;
  // Stock "is upright" (0x400ffb1c): Z < -0x7FF.
  ESP_LOGI(TAG, "Accel X=%+.2f g Y=%+.2f g Z=%+.2f g (counts %d %d %d) upright=%s", this->accel_[0] / 4096.0f,
           this->accel_[1] / 4096.0f, this->accel_[2] / 4096.0f, this->accel_[0], this->accel_[1], this->accel_[2],
           YESNO(this->accel_upright()));
}

void RiotProbe::status_leds_setup_() {
  // Stock hal_check_riot_hardware (RIoT branch): low-speed timer 0, 12-bit, 171 Hz; ch0 GPIO23 (fixed 50 %
  // reference), ch1 GPIO13 (Wi-Fi), ch2 GPIO32 (lock), all at duty 0x800 = in phase = off. Each window is a red/green
  // LED anti-parallel between GPIO23 and its pin, so the pin's duty/phase against GPIO23 picks the colour.
  // ESPHome's own LEDC outputs use the high-speed group, so the low-speed group is free.
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_12_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 171;
  timer.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t err = ledc_timer_config(&timer);
  static const int PINS[3] = {23, 13, 32};
  for (int ch = 0; ch < 3 && err == ESP_OK; ch++) {
    ledc_channel_config_t conf{};
    conf.gpio_num = PINS[ch];
    conf.speed_mode = LEDC_LOW_SPEED_MODE;
    conf.channel = static_cast<ledc_channel_t>(ch);
    conf.timer_sel = LEDC_TIMER_0;
    conf.duty = STATUS_HALF;
    conf.hpoint = 0;
    err = ledc_channel_config(&conf);
  }
  this->status_ok_ = err == ESP_OK;
  ESP_LOGI(TAG, "Status LEDs (GPIO23 ref, GPIO13 Wi-Fi, GPIO32 lock): %s", this->status_ok_ ? "ok" : esp_err_to_name(err));
}

void RiotProbe::set_status_indicator(uint8_t window, uint8_t state) {
  // Mirrors stock 0x40139c34: 0 off (in phase), 1 green (100 %), 2 red (0 %), 3 amber (anti-phase 50 %).
  if (window > 1 || state > 3 || this->variant_ != "RIoT")
    return;
  // Set up on first use, after every other component (including ESPHome's own LEDC outputs) has started.
  if (!this->status_ok_)
    this->status_leds_setup_();
  if (!this->status_ok_)
    return;
  static const uint32_t DUTY[4] = {STATUS_HALF, STATUS_FULL, 0, STATUS_HALF};
  static const uint32_t HPOINT[4] = {0, 0, 0, STATUS_HALF};
  ledc_channel_t ch = window == 0 ? LEDC_CHANNEL_1 : LEDC_CHANNEL_2;
  ledc_set_duty_with_hpoint(LEDC_LOW_SPEED_MODE, ch, DUTY[state], HPOINT[state]);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
  static const char *const NAMES[4] = {"off", "green", "red", "amber"};
  ESP_LOGI(TAG, "Status %s window -> %s", window == 0 ? "Wi-Fi" : "lock", NAMES[state]);
  for (int c = 0; c < 3; c++)
    ESP_LOGI(TAG, "  LS ch%d: duty %" PRIu32 " hpoint %d", c, ledc_get_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t) c),
             ledc_get_hpoint(LEDC_LOW_SPEED_MODE, (ledc_channel_t) c));
  ESP_LOGI(TAG, "  LS timer0 freq %" PRIu32 " Hz; GPIO23/13/32 level %d/%d/%d", ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0),
           gpio_get_level(GPIO_NUM_23), gpio_get_level(GPIO_NUM_13), gpio_get_level(GPIO_NUM_32));
}

void RiotProbe::dump_expander() {
  this->dump(EXPANDER_ADDR, 0x00, 0x08);
  this->dump(EXPANDER_ADDR, 0x10, 0x04);
}

void RiotProbe::arm(bool armed) {
  if (!armed) {
    if (this->armed_)
      this->restore();
    this->armed_ = false;
    ESP_LOGI(TAG, "Expander writes DISARMED");
    return;
  }
  if (!this->snapshot_ok_ || this->chip_id_ != AW9523B_ID) {
    ESP_LOGE(TAG, "Refusing to arm: expander not identified as AW9523B (ID=0x%02X)", this->chip_id_);
    return;
  }
  this->armed_ = true;
  ESP_LOGW(TAG, "Expander writes ARMED. Each change auto-reverts after %" PRIu32 " ms. Pins seen toggling (%04X) are "
                "never driven.",
           this->write_timeout_, this->seen_toggling_);
}

bool RiotProbe::guarded_write_(uint8_t reg, uint8_t value) {
  if (!this->armed_ || !this->snapshot_ok_ || this->chip_id_ != AW9523B_ID || this->address_ != EXPANDER_ADDR) {
    ESP_LOGE(TAG, "Write blocked (armed=%d, id=0x%02X)", this->armed_, this->chip_id_);
    return false;
  }
  if (reg != REG_OUT0 && reg != REG_OUT1 && reg != REG_CFG0 && reg != REG_CFG1 && reg != REG_GCR) {
    ESP_LOGE(TAG, "Write to register 0x%02X blocked", reg);
    return false;
  }
  ESP_LOGI(TAG, "Write expander reg 0x%02X <- 0x%02X", reg, value);
  return this->write_reg_(reg, value);
}

bool RiotProbe::drive_pin(uint8_t pin, bool high) {
  if (pin > 15) {
    ESP_LOGE(TAG, "Pin %u out of range", pin);
    return false;
  }
  uint16_t bit = 1u << pin;
  if (NEVER_DRIVE & bit) {
    ESP_LOGE(TAG, "Refusing to drive P%u_%u: the stock firmware never changes it (P0_6 wedged the bus)", pin / 8,
             pin % 8);
    return false;
  }
  if (this->seen_toggling_ & bit) {
    ESP_LOGE(TAG, "Refusing to drive P%u_%u: it has been seen acting as an input", pin / 8, pin % 8);
    return false;
  }
  uint8_t port = pin / 8, mask = 1u << (pin % 8);
  uint8_t out, cfg;
  if (!this->read_reg_(port ? REG_OUT1 : REG_OUT0, &out) || !this->read_reg_(port ? REG_CFG1 : REG_CFG0, &cfg))
    return false;
  out = high ? (out | mask) : (out & ~mask);
  cfg &= ~mask;  // 0 = output
  if (!this->armed_) {
    ESP_LOGE(TAG, "Write blocked: not armed");
    return false;
  }
  // Mark the pin and schedule the revert before writing, so a half-done write still gets undone.
  this->driven_mask_ |= bit;
  this->set_timeout("revert", this->write_timeout_, [this]() { this->restore(); });
  // Set the level first so the pin comes up in the requested state.
  if (!this->guarded_write_(port ? REG_OUT1 : REG_OUT0, out) ||
      !this->guarded_write_(port ? REG_CFG1 : REG_CFG0, cfg)) {
    this->restore();
    return false;
  }
  ESP_LOGW(TAG, "Driving P%u_%u %s (auto-revert in %" PRIu32 " ms)", port, pin % 8, high ? "HIGH" : "LOW",
           this->write_timeout_);
  return true;
}

bool RiotProbe::set_p0_push_pull(bool push_pull) {
  uint8_t gcr;
  if (!this->read_reg_(REG_GCR, &gcr))
    return false;
  gcr = push_pull ? (gcr | GCR_P0_PUSH_PULL) : (gcr & ~GCR_P0_PUSH_PULL);
  if (!this->guarded_write_(REG_GCR, gcr))
    return false;
  this->gcr_modified_ = true;
  this->set_timeout("revert", this->write_timeout_, [this]() { this->restore(); });
  return true;
}

bool RiotProbe::set_button_inputs(bool on) {
  if (!on) {
    this->restore();
    return true;
  }
  // Only the five button pins, exactly as the stock config. Releasing other pins (P0_6) wedged the bus.
  // Still gated behind arm and auto-reverted like every other write.
  if (!this->armed_) {
    ESP_LOGE(TAG, "Write blocked: not armed");
    return false;
  }
  // Mark modified before writing so a failed write is still reverted.
  this->cfg_modified_ = true;
  if (!this->guarded_write_(REG_CFG0, this->snap_cfg_[0] | CFG0_BUTTONS)) {
    ESP_LOGE(TAG, "Could not switch button pins to INPUT; reverting");
    this->restore();
    return false;
  }
  // Re-baseline inputs so pins settling to their idle (pulled) level aren't counted as button activity.
  this->have_inputs_ = false;
  this->set_timeout("revert", this->write_timeout_, [this]() { this->restore(); });
  ESP_LOGW(TAG, "Button pins P0_0-P0_4 set to INPUT (auto-revert in %" PRIu32 " ms). Press buttons now.",
           this->write_timeout_);
  return true;
}

void RiotProbe::restore() {
  this->cancel_timeout("revert");
  if (!this->snapshot_ok_ || (this->driven_mask_ == 0 && !this->gcr_modified_ && !this->cfg_modified_))
    return;
  // Go back to inputs first, then restore levels and mode. Restore writes skip the arm check.
  bool ok = this->write_reg_(REG_CFG0, this->snap_cfg_[0]) && this->write_reg_(REG_CFG1, this->snap_cfg_[1]) &&
            this->write_reg_(REG_OUT0, this->snap_out_[0]) && this->write_reg_(REG_OUT1, this->snap_out_[1]) &&
            this->write_reg_(REG_GCR, this->snap_gcr_);
  ESP_LOGW(TAG, "Expander restored to boot snapshot: %s", ok ? "ok" : "FAILED");
  if (ok) {
    this->driven_mask_ = 0;
    this->gcr_modified_ = false;
    this->cfg_modified_ = false;
    // Take a fresh baseline so pins settling back to input level aren't recorded as button activity.
    this->have_inputs_ = false;
  }
}

void RiotProbe::probe_sd() {
  // Same slot setup as stock hatch_sd_init (0x4010e6a4): width 1, internal pull-ups. Default 20 MHz, not stock's
  // 40 MHz, to keep the probe gentle.
  if (this->sd_busy_.exchange(true)) {
    ESP_LOGW(TAG, "SD: a listing is still running");
    return;
  }
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SD: host init failed: %s", esp_err_to_name(err));
    this->sd_busy_ = false;
    return;
  }
  err = sdmmc_host_init_slot(host.slot, &slot);
  if (err == ESP_OK) {
    sdmmc_card_t card{};
    uint32_t start = millis();
    err = sdmmc_card_init(&host, &card);
    if (err == ESP_OK) {
      uint64_t bytes = uint64_t(card.csd.capacity) * card.csd.sector_size;
      ESP_LOGW(TAG, "SD: card OK in %" PRIu32 " ms: %s '%s', %" PRIu32 " MB, OCR 0x%08" PRIX32 ", %d kHz",
               millis() - start, card.is_mmc ? "MMC" : (card.ocr & (1u << 30) ? "SDHC/SDXC" : "SDSC"),
               card.cid.name, (uint32_t) (bytes >> 20), card.ocr, card.max_freq_khz);
    } else {
      ESP_LOGW(TAG, "SD: no card response after %" PRIu32 " ms: %s (is SD power P1_1 on?)", millis() - start,
               esp_err_to_name(err));
    }
  } else {
    ESP_LOGE(TAG, "SD: slot init failed: %s", esp_err_to_name(err));
  }
  sdmmc_host_deinit();
  this->sd_busy_ = false;
}

// ---- Read-only FatFs disk driver ----
// Stands in for IDF's ff_diskio_register_sdmmc. The volume always reports STA_PROTECT, so FatFs refuses any
// open/mount in a write mode with FR_WRITE_PROTECTED, and the write hook fails as a second line of defence.
static sdmmc_card_t *s_ro_card = nullptr;
static uint32_t s_ro_writes_refused = 0;

static DSTATUS ro_disk_status(BYTE pdrv) { return STA_PROTECT; }
static DRESULT ro_disk_read(BYTE pdrv, BYTE *buff, uint32_t sector, unsigned count) {
  return sdmmc_read_sectors(s_ro_card, buff, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
}
static DRESULT ro_disk_write(BYTE pdrv, const BYTE *buff, uint32_t sector, unsigned count) {
  s_ro_writes_refused++;
  return RES_WRPRT;
}
static DRESULT ro_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  switch (cmd) {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *static_cast<DWORD *>(buff) = s_ro_card->csd.capacity;
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD *>(buff) = s_ro_card->csd.sector_size;
      return RES_OK;
    default:
      return RES_ERROR;
  }
}
static const ff_diskio_impl_t RO_DISKIO = {ro_disk_status, ro_disk_status, ro_disk_read, ro_disk_write,
                                           ro_disk_ioctl};

struct SdListState {
  uint8_t max_depth;
  uint16_t max_entries;
  uint16_t entries{0};
  uint32_t files{0}, dirs{0};
  uint64_t bytes{0};
  bool truncated{false};
};

static bool ends_with_ci(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

struct WavInfo {
  uint16_t format{0}, channels{0}, bits{0};
  uint32_t rate{0}, data_size{0};
};

// Walk a RIFF/WAVE file's chunks (skipping JUNK, LIST, ...) up to the start of the data chunk.
static bool read_wav_header(FIL *f, WavInfo &w) {
  uint8_t h[16];
  UINT got = 0;
  auto le32 = [](const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); };
  if (f_read(f, h, 12, &got) != FR_OK || got != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0)
    return false;
  bool have_fmt = false;
  for (int i = 0; i < 16; i++) {
    if (f_read(f, h, 8, &got) != FR_OK || got != 8)
      return false;
    uint32_t size = le32(h + 4), used = 0;
    if (memcmp(h, "data", 4) == 0) {
      w.data_size = size;
      return have_fmt;
    }
    if (memcmp(h, "fmt ", 4) == 0) {
      uint8_t b[16];
      if (size < 16 || f_read(f, b, 16, &got) != FR_OK || got != 16)
        return false;
      w.format = b[0] | b[1] << 8;
      w.channels = b[2] | b[3] << 8;
      w.rate = le32(b + 4);
      w.bits = b[14] | b[15] << 8;
      have_fmt = true;
      used = 16;
    }
    if (f_lseek(f, f_tell(f) + (size - used) + (size & 1)) != FR_OK)
      return false;
  }
  return false;
}

// Log the format from a WAV header.
static void log_wav_format(const char *path) {
  FIL f;
  WavInfo w;
  if (f_open(&f, path, FA_READ) != FR_OK)
    return;
  bool ok = read_wav_header(&f, w);
  f_close(&f);
  if (!ok) {
    ESP_LOGI(TAG, "SD:        not a readable RIFF/WAVE header");
    return;
  }
  ESP_LOGI(TAG, "SD:        WAVE format %u, %u ch, %" PRIu32 " Hz, %u-bit, %" PRIu32 " data bytes", w.format,
           w.channels, w.rate, w.bits, w.data_size);
}

// Log a short text file's contents (first 120 bytes, one line).
static void log_text(const char *path) {
  FIL f;
  char buf[121];
  UINT got = 0;
  if (f_open(&f, path, FA_READ) != FR_OK)
    return;
  FRESULT res = f_read(&f, buf, sizeof(buf) - 1, &got);
  f_close(&f);
  if (res != FR_OK)
    return;
  buf[got] = 0;
  for (UINT i = 0; i < got; i++) {
    if (buf[i] == '\r' || buf[i] == '\n')
      buf[i] = ' ';
  }
  ESP_LOGI(TAG, "SD:        text: '%s'", buf);
}

// Pace logging below the UART rate. The main loop drains logs from other tasks in one unbounded loop, writing each
// line to the 115200-baud UART (~11 ms per line). At 5 ms per entry the drain never finished, the main loop stopped
// feeding the 5 s task watchdog, and the unit reset (seen on hardware 2026-10-14).
static void log_pace() { vTaskDelay(pdMS_TO_TICKS(30)); }

// List `path` (a buffer of `cap` bytes holding "N:/..." of length `len`), descending while depth < max_depth.
// Directories are always logged; files only up to max_entries, but every file is counted, so the per-directory
// and overall totals cover the whole card.
static FRESULT list_dir(char *path, size_t len, size_t cap, uint8_t depth, SdListState &st) {
  auto dir = std::make_unique<FF_DIR>();
  auto fi = std::make_unique<FILINFO>();
  FRESULT res = f_opendir(dir.get(), path);
  if (res != FR_OK)
    return res;
  uint32_t files = 0, n_mp3 = 0, n_wav = 0;
  uint64_t bytes = 0;
  while ((res = f_readdir(dir.get(), fi.get())) == FR_OK && fi->fname[0] != 0) {
    snprintf(path + len, cap - len, "/%s", fi->fname);
    const char *shown = path + 2;  // drop the "N:" drive prefix
    if (fi->fattrib & AM_DIR) {
      st.dirs++;
      bool descend = depth + 1 < st.max_depth;
      ESP_LOGI(TAG, "SD: %-60s  <dir>%s", shown, descend ? "" : "  (not descended)");
      log_pace();
      if (descend) {
        FRESULT sub = list_dir(path, strlen(path), cap, depth + 1, st);
        if (sub != FR_OK)
          ESP_LOGW(TAG, "SD: listing %s failed: FatFs error %d", shown, sub);
      }
    } else {
      files++;
      bytes += fi->fsize;
      n_mp3 += ends_with_ci(fi->fname, ".mp3");
      n_wav += ends_with_ci(fi->fname, ".wav");
      if (st.entries < st.max_entries) {
        st.entries++;
        ESP_LOGI(TAG, "SD: %-60s  %10" PRIu32 " B  %04u-%02u-%02u %02u:%02u%s", shown, (uint32_t) fi->fsize,
                 1980 + (fi->fdate >> 9), (fi->fdate >> 5) & 15, fi->fdate & 31, fi->ftime >> 11,
                 (fi->ftime >> 5) & 63, (fi->fattrib & (AM_HID | AM_SYS)) ? "  hidden/system" : "");
        if (ends_with_ci(fi->fname, ".wav")) {
          log_wav_format(path);
        } else if (ends_with_ci(fi->fname, ".txt") && fi->fsize <= 512) {
          log_text(path);
        }
        log_pace();
      } else {
        st.truncated = true;
      }
    }
    path[len] = 0;
  }
  f_closedir(dir.get());
  st.files += files;
  st.bytes += bytes;
  ESP_LOGI(TAG, "SD: [%s] %" PRIu32 " files (%" PRIu32 " mp3, %" PRIu32 " wav), %" PRIu32 " MB", len > 2 ? path + 2 : "/",
           files, n_mp3, n_wav, (uint32_t) (bytes >> 20));
  log_pace();
  return res;
}

// The card on the stock slot, mounted read-only through RO_DISKIO. Unmounts and releases the host when destroyed.
class ReadOnlySdVolume {
 public:
  ~ReadOnlySdVolume() {
    if (this->registered_) {
      f_mount(nullptr, this->drv_, 0);
      ff_diskio_unregister(this->pdrv_);
      s_ro_card = nullptr;
    }
    heap_caps_free(this->fs_);
    if (this->host_)
      sdmmc_host_deinit();
  }

  bool mount() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "SD: host init failed: %s", esp_err_to_name(err));
      return false;
    }
    this->host_ = true;
    if ((err = sdmmc_host_init_slot(host.slot, &slot)) != ESP_OK) {
      ESP_LOGE(TAG, "SD: slot init failed: %s", esp_err_to_name(err));
      return false;
    }
    if ((err = sdmmc_card_init(&host, &this->card_)) != ESP_OK) {
      ESP_LOGW(TAG, "SD: no card response: %s (is SD power P1_1 on?)", esp_err_to_name(err));
      return false;
    }
    this->fs_ = static_cast<FATFS *>(heap_caps_calloc(1, sizeof(FATFS), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (this->fs_ == nullptr || ff_diskio_get_drive(&this->pdrv_) != ESP_OK) {
      ESP_LOGE(TAG, "SD: no memory or free FatFs drive");
      return false;
    }
    s_ro_card = &this->card_;
    s_ro_writes_refused = 0;
    ff_diskio_register(this->pdrv_, &RO_DISKIO);
    this->drv_[0] = '0' + this->pdrv_;
    this->drv_[1] = ':';
    FRESULT res = f_mount(this->fs_, this->drv_, 1);
    this->registered_ = true;  // FatFs keeps the work area registered even when the mount fails
    if (res != FR_OK) {
      ESP_LOGE(TAG, "SD: mount failed: FatFs error %d (13 = no FAT volume, e.g. exFAT)", res);
      return false;
    }
    static const char *const FS_TYPES[] = {"?", "FAT12", "FAT16", "FAT32", "exFAT"};
    ESP_LOGW(TAG, "SD: mounted read-only: %s, cluster %u KB", FS_TYPES[this->fs_->fs_type <= 4 ? this->fs_->fs_type : 0],
             this->cluster_bytes() / 1024);
    return true;
  }

  const char *drive() const { return this->drv_; }
  FATFS *fs() const { return this->fs_; }
  uint32_t cluster_bytes() const { return uint32_t(this->fs_->csize) * this->card_.csd.sector_size; }

 protected:
  sdmmc_card_t card_{};
  FATFS *fs_{nullptr};
  BYTE pdrv_{FF_DRV_NOT_USED};
  char drv_[3]{};
  bool host_{false};
  bool registered_{false};
};

bool RiotProbe::sd_start_task_(const char *name, TaskFunction_t task) {
  if (this->sd_busy_.exchange(true)) {
    ESP_LOGW(TAG, "SD: a probe, listing or playback is already running");
    return false;
  }
  // SD power is expander P1_1 (logical pin 9). Driving it needs arm and auto-reverts after write_timeout, which
  // also bounds how long the task has before the card loses power.
  if (!this->drive_pin(9, true)) {
    ESP_LOGE(TAG, "SD: could not power the card on P1_1 (not armed, or the expander write failed; try again)");
    this->sd_busy_ = false;
    return false;
  }
  ESP_LOGW(TAG, "SD: powered; %s starts in 1 s (stock settle time) and must finish within %" PRIu32 " ms", name,
           this->write_timeout_);
  this->set_timeout("sd_task", 1000, [this, name, task]() {
    // 12 KB: FatFs plus the MP3 decoder (ESPHome's own decoder task uses 5 KB).
    if (xTaskCreate(task, name, 12288, this, 2, nullptr) != pdPASS) {
      ESP_LOGE(TAG, "SD: could not start the %s task", name);
      this->sd_busy_ = false;
    }
  });
  return true;
}

void RiotProbe::list_sd(const std::string &root, uint8_t max_depth, uint16_t max_entries) {
  if (this->sd_busy_) {
    ESP_LOGW(TAG, "SD: a probe, listing or playback is already running");
    return;
  }
  // Normalise to "" (card root) or "/a/b" without a trailing slash.
  this->sd_root_ = root;
  while (!this->sd_root_.empty() && this->sd_root_.back() == '/')
    this->sd_root_.pop_back();
  if (!this->sd_root_.empty() && this->sd_root_[0] != '/')
    this->sd_root_.insert(0, "/");
  this->sd_max_depth_ = max_depth < 1 ? 1 : max_depth;
  this->sd_max_entries_ = max_entries;
  this->sd_start_task_("sd_list", RiotProbe::sd_list_task_);
}

void RiotProbe::sd_list_task_(void *arg) {
  auto *self = static_cast<RiotProbe *>(arg);
  self->sd_list_();
  self->sd_busy_ = false;
  vTaskDelete(nullptr);
}

void RiotProbe::sd_list_() {
  uint32_t start = millis();
  {
    ReadOnlySdVolume vol;
    if (vol.mount()) {
      SdListState st{this->sd_max_depth_, this->sd_max_entries_};
      char path[300];
      snprintf(path, sizeof(path), "%s%s", vol.drive(), this->sd_root_.c_str());
      FRESULT res = list_dir(path, strlen(path), sizeof(path), 0, st);
      if (res != FR_OK)
        ESP_LOGW(TAG, "SD: listing stopped: FatFs error %d", res);
      ESP_LOGW(TAG, "SD: total %" PRIu32 " files, %" PRIu32 " dirs, %" PRIu32 " MB%s", st.files, st.dirs,
               (uint32_t) (st.bytes >> 20),
               st.truncated ? " (entry limit reached: not every file was logged, totals cover all)" : "");

      DWORD free_clusters = 0;
      FATFS *fsp = nullptr;
      if (f_getfree(vol.drive(), &free_clusters, &fsp) == FR_OK) {
        uint64_t cluster = vol.cluster_bytes();
        ESP_LOGW(TAG, "SD: volume %" PRIu32 " MB, free %" PRIu32 " MB",
                 (uint32_t) (((vol.fs()->n_fatent - 2) * cluster) >> 20), (uint32_t) ((free_clusters * cluster) >> 20));
      }
    }
  }
  ESP_LOGW(TAG, "SD: done in %" PRIu32 " ms; writes refused by the read-only driver: %" PRIu32 " (expected 0)",
           millis() - start, s_ro_writes_refused);
}

void RiotProbe::play_sd_file(speaker::Speaker *spk, const std::string &path, float volume, uint16_t max_seconds,
                            bool mix_to_mono) {
  if (this->sd_busy_) {
    ESP_LOGW(TAG, "SD: a probe, listing or playback is already running");
    return;
  }
  if (spk == nullptr || !spk->is_stopped()) {
    ESP_LOGE(TAG, "SD: speaker missing or busy");
    return;
  }
  this->sd_speaker_ = spk;
  this->sd_mix_to_mono_ = mix_to_mono;
  this->sd_play_path_ = path.empty() || path[0] == '/' ? path : "/" + path;
  // The card and amp power revert write_timeout after the amp is switched on; leave a few seconds to drain.
  uint32_t window_s = this->write_timeout_ / 1000;
  this->sd_play_seconds_ = std::min<uint32_t>(max_seconds, window_s > 5 ? window_s - 5 : 1);
  spk->set_volume(volume);
  this->sd_start_task_("sd_play", RiotProbe::sd_play_task_);
}

void RiotProbe::sd_play_task_(void *arg) {
  auto *self = static_cast<RiotProbe *>(arg);
  self->sd_play_();
  // Amp and card off together (restore reverts every driven pin). Runs on the main loop, after the unmount; the
  // card stays busy until then so a new SD task can't start just before the power goes.
  self->defer([self]() {
    self->restore();
    self->sd_busy_ = false;
  });
  vTaskDelete(nullptr);
}

void RiotProbe::sd_play_() {
  ReadOnlySdVolume vol;
  if (!vol.mount())
    return;
  std::string full = std::string(vol.drive()) + this->sd_play_path_;
  FIL f;
  FRESULT res = f_open(&f, full.c_str(), FA_READ);
  if (res != FR_OK) {
    ESP_LOGE(TAG, "SD: cannot open %s: FatFs error %d", this->sd_play_path_.c_str(), res);
    return;
  }
  if (ends_with_ci(this->sd_play_path_.c_str(), ".mp3")) {
    this->sd_play_mp3_(&f);
    f_close(&f);
    return;
  }
  WavInfo w;
  if (!read_wav_header(&f, w) || w.format != 1 || w.bits != 16 || w.channels < 1 || w.channels > 2) {
    ESP_LOGE(TAG, "SD: %s is not 16-bit PCM mono/stereo WAV (format %u, %u ch, %u-bit)", this->sd_play_path_.c_str(),
             w.format, w.channels, w.bits);
    f_close(&f);
    return;
  }
  const uint32_t frame = 2u * w.channels;
  const bool mix = w.channels == 2 && this->sd_mix_to_mono_;
  const uint8_t out_channels = mix ? 1 : w.channels;
  const uint32_t out_frame = 2u * out_channels;
  uint32_t data_bytes = std::min<uint32_t>(w.data_size, f_size(&f) - f_tell(&f));
  const uint32_t audio_ms = uint64_t(data_bytes / frame) * 1000 / w.rate;
  data_bytes = std::min<uint32_t>(data_bytes, uint32_t(this->sd_play_seconds_) * w.rate * frame);
  ESP_LOGW(TAG, "SD: playing %s: %u ch, %" PRIu32 " Hz, 16-bit, %" PRIu32 " ms of audio; playing %" PRIu32
                " ms%s",
           this->sd_play_path_.c_str(), w.channels, w.rate, audio_ms, uint32_t(uint64_t(data_bytes / frame) * 1000 / w.rate),
           mix ? " (stereo mixed down to mono)" : (w.channels == 2 ? " (stereo passed through)" : ""));

  speaker::Speaker *spk = this->sd_speaker_;
  spk->set_audio_stream_info(audio::AudioStreamInfo(16, out_channels, w.rate));
  static constexpr size_t CHUNK = 4096;  // a multiple of both frame sizes
  auto *buf = static_cast<uint8_t *>(heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (buf == nullptr) {
    ESP_LOGE(TAG, "SD: no memory for the read buffer");
    f_close(&f);
    return;
  }

  uint32_t fed = 0, read_total = 0, read_us = 0, start = millis(), last_report = start, last_progress = start;
  bool amp_requested = false, stalled = false;
  while (read_total < data_bytes) {
    UINT got = 0;
    uint32_t t0 = micros();
    res = f_read(&f, buf, std::min<uint32_t>(CHUNK, data_bytes - read_total), &got);
    read_us += micros() - t0;
    if (res != FR_OK || got == 0) {
      ESP_LOGE(TAG, "SD: read failed at byte %" PRIu32 ": FatFs error %d", read_total, res);
      break;
    }
    read_total += got;
    size_t len = got;
    if (mix) {
      auto *s16 = reinterpret_cast<int16_t *>(buf);
      for (size_t i = 0; i < got / 4; i++)
        s16[i] = int16_t((int32_t(s16[2 * i]) + s16[2 * i + 1]) / 2);
      len = got / 2;
    }
    size_t off = 0;
    while (off < len) {
      size_t n = spk->play(buf + off, len - off, pdMS_TO_TICKS(100));
      off += n;
      if (n > 0) {
        last_progress = millis();
      } else if (millis() - last_progress > 3000) {
        stalled = true;
        break;
      }
    }
    fed += off;
    if (stalled) {
      ESP_LOGE(TAG, "SD: speaker took no data for 3 s; stopping");
      break;
    }
    // Amp on only once the I2S clocks run: enabling it first caused a loud pop. drive_pin runs on the main loop.
    if (!amp_requested && spk->is_running()) {
      amp_requested = true;
      this->defer([this]() { this->drive_pin(10, true); });
      ESP_LOGW(TAG, "SD: speaker running; amp (P1_2) on");
    }
    if (millis() - last_report >= 5000) {
      last_report = millis();
      ESP_LOGI(TAG, "SD: fed %" PRIu32 " ms of audio in %" PRIu32 " ms", uint32_t(uint64_t(fed / out_frame) * 1000 / w.rate),
               last_report - start);
    }
  }
  f_close(&f);
  heap_caps_free(buf);

  spk->finish();
  uint32_t drain_start = millis();
  while (!spk->is_stopped() && millis() - drain_start < 5000)
    vTaskDelay(pdMS_TO_TICKS(20));
  uint32_t wall = millis() - start;
  ESP_LOGW(TAG,
           "SD: playback done: %" PRIu32 " ms of audio in %" PRIu32 " ms wall (SD reads %" PRIu32
           " ms total, %" PRIu32 " KB/s); speaker %s; writes refused: %" PRIu32 " (expected 0)",
           uint32_t(uint64_t(fed / out_frame) * 1000 / w.rate), wall, read_us / 1000,
           read_us ? uint32_t(uint64_t(read_total) * 1000 / read_us) : 0, spk->is_stopped() ? "stopped" : "STILL RUNNING",
           s_ro_writes_refused);
}

// Decoder sink that sends decoded PCM to the speaker as 16-bit stereo. The speaker's stream info is set from the
// first decoded frame, before any audio reaches it. Mono is duplicated to both channels: 16-bit mono at 44.1 kHz
// played at half speed on this I2S speaker (hardware-map.md, "Playback from the card").
class SpeakerStereoSink : public audio::AudioSinkCallback {
 public:
  SpeakerStereoSink(speaker::Speaker *spk, audio::AudioDecoder *decoder) : spk_(spk), decoder_(decoder) {}
  ~SpeakerStereoSink() { heap_caps_free(this->scratch_); }

  size_t audio_sink_write(uint8_t *data, size_t length, TickType_t ticks_to_wait) override {
    if (this->failed_)
      return length;  // drop
    if (!this->configured_ && !this->configure_())
      return this->failed_ ? length : 0;
    if (!this->spk_->is_running()) {
      // Each play() on a stopped speaker retries start(); if setup keeps failing that floods the log, so give up.
      if (this->start_wait_ == 0) {
        this->start_wait_ = millis();
      } else if (millis() - this->start_wait_ > 2000) {
        ESP_LOGE(TAG, "SD: speaker did not start within 2 s");
        this->failed_ = true;
        return length;
      }
    }
    if (this->channels_ == 2) {
      size_t n = this->spk_->play(data, length, ticks_to_wait);
      this->out_bytes_ += n;
      if (n == 0 && !this->spk_->is_running())
        vTaskDelay(pdMS_TO_TICKS(100));  // let a start attempt finish before retrying
      return n;
    }
    // Mono: duplicate each sample into L and R, then block until the speaker has taken all of it, so the input
    // consumed always matches whole samples.
    size_t samples = std::min<size_t>(length / 2, SCRATCH_SAMPLES);
    auto *in = reinterpret_cast<const int16_t *>(data);
    for (size_t i = 0; i < samples; i++)
      this->scratch_[2 * i] = this->scratch_[2 * i + 1] = in[i];
    const auto *out = reinterpret_cast<const uint8_t *>(this->scratch_);
    size_t total = samples * 4, off = 0;
    uint32_t last = millis();
    while (off < total) {
      size_t n = this->spk_->play(out + off, total - off, pdMS_TO_TICKS(50));
      off += n;
      if (n > 0) {
        last = millis();
      } else if (millis() - last > 3000) {
        this->failed_ = true;
        ESP_LOGE(TAG, "SD: speaker took no data for 3 s");
        break;
      } else if (!this->spk_->is_running()) {
        vTaskDelay(pdMS_TO_TICKS(100));  // let a start attempt finish before retrying
      }
    }
    this->out_bytes_ += off;
    return samples * 2;
  }

  bool failed() const { return this->failed_; }
  bool configured() const { return this->configured_; }
  uint8_t channels() const { return this->channels_; }
  uint32_t rate() const { return this->rate_; }
  uint32_t played_ms() const { return this->rate_ ? uint32_t(uint64_t(this->out_bytes_ / 4) * 1000 / this->rate_) : 0; }

 protected:
  static constexpr size_t SCRATCH_SAMPLES = 1024;

  bool configure_() {
    const auto &info = this->decoder_->get_audio_stream_info();
    if (!info.has_value())
      return false;
    this->channels_ = info.value().get_channels();
    this->rate_ = info.value().get_sample_rate();
    uint8_t bits = info.value().get_bits_per_sample();
    ESP_LOGW(TAG,
             "SD: decoded stream: %u ch, %" PRIu32 " Hz, %u-bit%s; internal RAM free %u KB (largest %u KB), "
             "PSRAM free %u KB",
             this->channels_, this->rate_, bits, this->channels_ == 1 ? " (sent as stereo)" : "",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    if (bits != 16 || this->channels_ < 1 || this->channels_ > 2) {
      ESP_LOGE(TAG, "SD: unsupported decoded format");
      this->failed_ = true;
      return false;
    }
    if (this->channels_ == 1) {
      this->scratch_ = static_cast<int16_t *>(heap_caps_malloc(SCRATCH_SAMPLES * 4, MALLOC_CAP_INTERNAL));
      if (this->scratch_ == nullptr) {
        ESP_LOGE(TAG, "SD: no memory for the stereo buffer");
        this->failed_ = true;
        return false;
      }
    }
    this->spk_->set_audio_stream_info(audio::AudioStreamInfo(16, 2, this->rate_));
    this->configured_ = true;
    return true;
  }

  speaker::Speaker *spk_;
  audio::AudioDecoder *decoder_;
  int16_t *scratch_{nullptr};
  uint64_t out_bytes_{0};
  uint32_t start_wait_{0};
  uint32_t rate_{0};
  uint8_t channels_{0};
  bool configured_{false};
  bool failed_{false};
};

void RiotProbe::sd_play_mp3_(FIL *f) {
  speaker::Speaker *spk = this->sd_speaker_;
  const uint32_t limit_ms = uint32_t(this->sd_play_seconds_) * 1000;
  ESP_LOGW(TAG, "SD: playing %s: MP3, %" PRIu32 " KB; playing up to %" PRIu32 " ms", this->sd_play_path_.c_str(),
           (uint32_t) (f_size(f) / 1024), limit_ms);

  std::shared_ptr<ring_buffer::RingBuffer> input = ring_buffer::RingBuffer::create(32 * 1024);
  auto *buf = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (input == nullptr || buf == nullptr) {
    ESP_LOGE(TAG, "SD: no memory for the MP3 buffers");
    heap_caps_free(buf);
    return;
  }
  std::weak_ptr<ring_buffer::RingBuffer> weak_input = input;
  audio::AudioDecoder decoder(4096, 8192);
  SpeakerStereoSink sink(spk, &decoder);
  if (decoder.add_source(weak_input) != ESP_OK || decoder.add_sink(&sink) != ESP_OK ||
      decoder.start(audio::AudioFileType::MP3) != ESP_OK) {
    ESP_LOGE(TAG, "SD: MP3 decoder setup failed");
    heap_caps_free(buf);
    return;
  }

  uint32_t start = millis(), last_report = start, read_us = 0, read_total = 0;
  size_t pending = 0, off = 0;
  bool eof = false, amp_requested = false;
  audio::AudioDecoderState state = audio::AudioDecoderState::DECODING;
  while (true) {
    if (!eof && pending == 0) {
      UINT got = 0;
      uint32_t t0 = micros();
      FRESULT res = f_read(f, buf, 4096, &got);
      read_us += micros() - t0;
      if (res != FR_OK) {
        ESP_LOGE(TAG, "SD: read failed at byte %" PRIu32 ": FatFs error %d", read_total, res);
        eof = true;
      } else if (got == 0) {
        eof = true;
      }
      read_total += got;
      pending = got;
      off = 0;
    }
    if (pending > 0) {
      size_t n = input->write_without_replacement(buf + off, pending, 0);
      off += n;
      pending -= n;
    }
    state = decoder.decode(eof && pending == 0);
    if (state != audio::AudioDecoderState::DECODING || sink.failed())
      break;
    if (!amp_requested && spk->is_running()) {
      // Amp on only once the I2S clocks run: enabling it first caused a loud pop. drive_pin runs on the main loop.
      amp_requested = true;
      this->defer([this]() { this->drive_pin(10, true); });
      ESP_LOGW(TAG, "SD: speaker running; amp (P1_2) on");
    }
    if (sink.played_ms() >= limit_ms)
      break;
    if (millis() - last_report >= 5000) {
      last_report = millis();
      ESP_LOGI(TAG, "SD: decoded and fed %" PRIu32 " ms of audio in %" PRIu32 " ms", sink.played_ms(),
               last_report - start);
    }
  }
  heap_caps_free(buf);

  spk->finish();
  uint32_t drain_start = millis();
  while (!spk->is_stopped() && millis() - drain_start < 5000)
    vTaskDelay(pdMS_TO_TICKS(20));
  static const char *const STATES[] = {"decoding (stopped at the time limit)", "finished", "FAILED"};
  ESP_LOGW(TAG,
           "SD: MP3 playback done: decoder %s; %" PRIu32 " ms of audio in %" PRIu32 " ms wall; read %" PRIu32
           " KB (SD %" PRIu32 " ms); speaker %s; writes refused: %" PRIu32 " (expected 0)",
           sink.failed() ? "sink failed" : STATES[uint8_t(state) <= 2 ? uint8_t(state) : 2], sink.played_ms(),
           millis() - start, read_total / 1024, read_us / 1000, spk->is_stopped() ? "stopped" : "STILL RUNNING",
           s_ro_writes_refused);
}

void RiotProbe::on_shutdown() {
  this->cancel_timeout("revert");
  this->bus_.release();
}

void RiotProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "RIoT probe:");
  ESP_LOGCONFIG(TAG, "  Bus: bit-banged, SDA GPIO%u, SCL GPIO%u, ~%" PRIu32 " kHz, expander 0x%02X", this->sda_pin_,
                this->scl_pin_, 500 / this->bus_.half_period_us(), EXPANDER_ADDR);
  ESP_LOGCONFIG(TAG, "  Variant: %s (GPIO13=%u GPIO32=%u GPIO23=%u)", this->variant_.c_str(), this->sense_[0],
                this->sense_[1], this->sense_[2]);
  ESP_LOGCONFIG(TAG, "  Reads: on GPIO34 INT low, else every %" PRIu32 " ms", this->fallback_interval_);
  ESP_LOGCONFIG(TAG, "  Buttons at boot: %s", YESNO(this->buttons_at_boot_));
  ESP_LOGCONFIG(TAG, "  Poll interval: %" PRIu32 " ms, write timeout: %" PRIu32 " ms", this->poll_interval_, this->write_timeout_);
}

}  // namespace riot_probe
}  // namespace esphome
