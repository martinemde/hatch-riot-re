#include "riot_probe.h"

#include <cinttypes>

#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace riot_probe {

static const char *const TAG = "riot_probe";

// Only this address may ever be written, and only these registers.
static const uint8_t EXPANDER_ADDR = 0x58;
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
  // Wait until every bus is up before the first I2C access.
  this->ready_at_ = millis() + 2000;
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
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SD: host init failed: %s", esp_err_to_name(err));
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
}

void RiotProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "RIoT probe:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Variant: %s (GPIO13=%u GPIO32=%u GPIO23=%u)", this->variant_.c_str(), this->sense_[0],
                this->sense_[1], this->sense_[2]);
  ESP_LOGCONFIG(TAG, "  Reads: on GPIO34 INT low, else every %" PRIu32 " ms", this->fallback_interval_);
  ESP_LOGCONFIG(TAG, "  Buttons at boot: %s", YESNO(this->buttons_at_boot_));
  ESP_LOGCONFIG(TAG, "  Poll interval: %" PRIu32 " ms, write timeout: %" PRIu32 " ms", this->poll_interval_, this->write_timeout_);
}

}  // namespace riot_probe
}  // namespace esphome
