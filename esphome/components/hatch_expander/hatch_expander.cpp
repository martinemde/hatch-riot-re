#include "hatch_expander.h"

#include <cinttypes>

#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include "esphome/core/log.h"

namespace esphome::hatch_expander {

static const char *const TAG = "hatch_expander";

static const uint8_t REG_IN = 0x00, REG_OUT = 0x02, REG_CFG = 0x04, REG_INT = 0x06, REG_ID = 0x10, REG_GCR = 0x11;
static const uint8_t CHIP_ID = 0x23;
static const uint8_t GCR_P0_PUSH_PULL = 0x10;
// Bits the stock firmware's pin model covers, per port; everything else is left at its power-on state.
static const uint8_t STOCK_MASK[2] = {0x3F, 0x0F};
// Switching a load (LED driver, amp) makes the chip NACK for ~10-25 ms. Keep every transaction out of that window,
// and space retries.
static const uint32_t SETTLE_MS = 20, RETRY_GAP_MS = 10;
static const int WRITE_ATTEMPTS = 3;
// This bus can't take back-to-back transactions: on hardware ~50 % of reads failed back-to-back vs ~8 % with 5 ms
// between them, and a few reads came back wrong (0xFF) without an error. Space every transaction, retry reads, verify
// writes, and only accept input changes that two reads agree on.
static const uint32_t MIN_GAP_MS = 5;
static const int READ_ATTEMPTS = 3;
// Without an interrupt pin, don't read more often than this (binary sensors poll every loop).
static const uint32_t MIN_POLL_MS = 50;
// With an interrupt pin, still re-read occasionally in case an edge was missed.
static const uint32_t FALLBACK_REFRESH_MS = 1000;
// Longest a slave may hold SCL low (clock stretching) before we call it a bus error.
static const uint32_t STRETCH_TIMEOUT_US = 2000;

void HatchExpander::setup() {
  this->bb_init_();
  if (!this->bb_clear_())
    ESP_LOGW(TAG, "Bus not idle at boot (SDA=%d SCL=%d) after clearing", gpio_get_level((gpio_num_t) this->sda_),
             gpio_get_level((gpio_num_t) this->scl_));
  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->interrupt_pin_->attach_interrupt(&HatchExpander::gpio_intr, this, gpio::INTERRUPT_FALLING_EDGE);
    this->set_invalidate_on_read_(false);
    this->set_interval("refresh", FALLBACK_REFRESH_MS, [this]() { this->enable_loop(); });
  }
  this->disable_loop();
  // The bus is often unsettled right after boot (SCL held low, garbage scan). Try briefly now; if the chip doesn't
  // answer, keep retrying once a second instead of giving up. Pin modes and output levels requested meanwhile are
  // remembered and applied once it answers.
  for (int i = 0; i < 5 && !this->ready_; i++) {
    if (!this->try_init_())
      delay(100);
  }
  if (!this->ready_) {
    ESP_LOGW(TAG, "Expander not answering yet; retrying every second");
    this->status_set_warning();
    this->set_interval("init", 1000, [this]() {
      if (this->try_init_()) {
        this->status_clear_warning();
        this->cancel_interval("init");
      }
    });
  }
}

bool HatchExpander::try_init_() {
  this->init_attempts_++;
  uint8_t id = 0, gcr = 0;
  if (!this->read_regs_(REG_ID, &id, 1) || id != CHIP_ID)
    return false;
  if (!this->read_regs_(REG_OUT, this->out_, 2) || !this->read_regs_(REG_CFG, this->cfg_, 2) ||
      !this->read_regs_(REG_GCR, &gcr, 1))
    return false;
  this->ready_ = true;
  // Interrupts off for every stock pin, then back on for the configured inputs below.
  // (The interrupt-mask registers don't read back reliably on this chip, so they aren't verified.)
  this->update_reg_(REG_INT, STOCK_MASK[0], STOCK_MASK[0], false);
  this->update_reg_(REG_INT + 1, STOCK_MASK[1], STOCK_MASK[1], false);
  uint8_t want = this->p0_push_pull_ ? (gcr | GCR_P0_PUSH_PULL) : (gcr & ~GCR_P0_PUSH_PULL);
  if (want != gcr)
    this->write_reg_(REG_GCR, want);
  // Replay what the pins asked for before the chip answered: levels first, then direction.
  for (uint8_t pin = 0; pin < 16; pin++) {
    uint16_t bit = 1u << pin;
    if (this->out_set_ & bit)
      this->apply_output_(pin, this->out_desired_ & bit);
    if (this->mode_set_ & bit)
      this->apply_mode_(pin, this->mode_input_ & bit);
  }
  ESP_LOGI(TAG, "Expander found after %" PRIu32 " attempt(s)", this->init_attempts_);
  if (this->mode_input_)
    this->enable_loop();
  return true;
}

void IRAM_ATTR HatchExpander::gpio_intr(HatchExpander *arg) { arg->enable_loop_soon_any_context(); }

void HatchExpander::loop() {
  this->reset_pin_cache_();
  // INT stays asserted until the inputs are read; keep looping until it's released so no edge is stranded.
  if (this->interrupt_pin_ == nullptr || this->interrupt_pin_->digital_read())
    this->disable_loop();
}

void HatchExpander::on_shutdown() {
  this->ready_ = false;
  this->bb_sda_(true);
  this->bb_scl_(true);
}

void HatchExpander::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Hatch expander (stock AW9110B pin model, bit-banged I2C):\n"
                "  Address: 0x%02X  SDA: GPIO%u  SCL: GPIO%u  ~%" PRIu32 " kHz\n"
                "  Bus errors since boot: %" PRIu32,
                this->address_, this->sda_, this->scl_, 500 / this->half_period_us_, this->bus_errors_);
  LOG_PIN("  Interrupt pin: ", this->interrupt_pin_);
  ESP_LOGCONFIG(TAG, "  P0 push-pull: %s\n  Found: %s (after %" PRIu32 " attempt(s))\n  Now: OUT=%02X %02X CFG=%02X %02X",
                YESNO(this->p0_push_pull_), YESNO(this->ready_), this->init_attempts_, this->out_[1], this->out_[0],
                this->cfg_[1], this->cfg_[0]);
}

bool HatchExpander::digital_read_hw(uint8_t pin) {
  // Never report a read we don't trust: keep the cached inputs (initially all high = released) and retry later. The
  // buttons are active-low, so a failed or corrupt read reported as "low" would be a phantom press.
  if (!this->ready_)
    return true;
  if (this->interrupt_pin_ == nullptr && millis() - this->last_read_ < MIN_POLL_MS)
    return true;
  this->last_read_ = millis();
  uint8_t a[2], b[2];
  if (!this->read_regs_(REG_IN, a, 2) || !this->read_regs_(REG_IN, b, 2) || a[0] != b[0] || a[1] != b[1]) {
    if (++this->read_failures_ % 10 == 1)
      ESP_LOGW(TAG, "Input read failed or unstable (%" PRIu32 " so far); keeping last state", this->read_failures_);
    this->enable_loop();
    return true;
  }
  this->inputs_ = a[0] | (uint16_t(a[1]) << 8);
  return true;
}

void HatchExpander::digital_write_hw(uint8_t pin, bool value) {
  uint16_t bit = 1u << pin;
  this->out_set_ |= bit;
  this->out_desired_ = value ? (this->out_desired_ | bit) : (this->out_desired_ & ~bit);
  if (this->ready_)
    this->apply_output_(pin, value);
}

void HatchExpander::pin_mode(uint8_t pin, gpio::Flags flags) {
  uint16_t bit = 1u << pin;
  bool input = flags & gpio::FLAG_INPUT;
  this->mode_set_ |= bit;
  this->mode_input_ = input ? (this->mode_input_ | bit) : (this->mode_input_ & ~bit);
  if (this->ready_) {
    this->apply_mode_(pin, input);
    if (input)
      this->enable_loop();
  }
}

void HatchExpander::apply_output_(uint8_t pin, bool value) {
  uint8_t port = pin / 8, bit = 1u << (pin % 8);
  this->update_reg_(REG_OUT + port, bit, value ? bit : 0);
}

void HatchExpander::apply_mode_(uint8_t pin, bool input) {
  uint8_t port = pin / 8, bit = 1u << (pin % 8);
  this->update_reg_(REG_CFG + port, bit, input ? bit : 0);  // 1 = input
  this->update_reg_(REG_INT + port, bit, input ? 0 : bit, false);  // 0 = interrupt enabled
}

bool HatchExpander::update_reg_(uint8_t reg, uint8_t mask, uint8_t bits, bool verify) {
  // Read-modify-write against the chip itself (never a cached copy), then verify.
  uint8_t port = reg & 1;
  mask &= STOCK_MASK[port];
  for (int round = 1; round <= WRITE_ATTEMPTS; round++) {
    uint8_t cur;
    if (!this->read_regs_(reg, &cur, 1))
      continue;
    uint8_t value = (cur & ~mask) | (bits & mask);
    if (value != cur && !this->write_reg_(reg, value))
      continue;
    if (!verify)
      return true;
    uint8_t check;
    if (this->read_regs_(reg, &check, 1) && (check & mask) == (bits & mask)) {
      if ((reg & ~1) == REG_OUT)
        this->out_[port] = check;
      else if ((reg & ~1) == REG_CFG)
        this->cfg_[port] = check;
      return true;
    }
    ESP_LOGW(TAG, "Reg 0x%02X did not verify (round %d/%d)", reg, round, WRITE_ATTEMPTS);
  }
  this->status_set_warning();
  return false;
}

bool HatchExpander::write_reg_(uint8_t reg, uint8_t value) {
  for (int attempt = 1; attempt <= WRITE_ATTEMPTS; attempt++) {
    this->settle_();
    bool ok = this->bb_write_reg_(reg, value);
    this->mark_txn_(ok ? SETTLE_MS : RETRY_GAP_MS);
    if (ok)
      return true;
    ESP_LOGW(TAG, "Write reg 0x%02X <- 0x%02X failed (attempt %d/%d)", reg, value, attempt, WRITE_ATTEMPTS);
  }
  return false;
}

bool HatchExpander::read_regs_(uint8_t reg, uint8_t *data, size_t len) {
  for (int attempt = 1; attempt <= READ_ATTEMPTS; attempt++) {
    this->settle_();
    bool ok = this->bb_read_regs_(reg, data, len);
    this->mark_txn_(ok ? MIN_GAP_MS : RETRY_GAP_MS);
    if (ok)
      return true;
  }
  return false;
}

void HatchExpander::mark_txn_(uint32_t gap_ms) {
  uint32_t until = millis() + gap_ms;
  if (int32_t(until - this->quiet_until_) > 0)
    this->quiet_until_ = until;
}

void HatchExpander::settle_() {
  int32_t wait = int32_t(this->quiet_until_ - millis());
  if (wait > 0)
    delay(wait);
}

// ---------------------------------------------------------------- bit-banged I2C

void HatchExpander::bb_init_() {
  for (gpio_num_t pin : {(gpio_num_t) this->sda_, (gpio_num_t) this->scl_}) {
    gpio_reset_pin(pin);
    gpio_set_level(pin, 1);  // released
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    // Stock leaves the internal pull-ups off; the board has external ones.
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
}

void HatchExpander::bb_delay_() const { esp_rom_delay_us(this->half_period_us_); }

void HatchExpander::bb_sda_(bool high) { gpio_set_level((gpio_num_t) this->sda_, high ? 1 : 0); }

bool HatchExpander::bb_scl_(bool high) {
  gpio_set_level((gpio_num_t) this->scl_, high ? 1 : 0);
  if (!high)
    return true;
  // Wait for SCL to actually rise (slow edge or clock stretching).
  uint32_t start = micros();
  while (gpio_get_level((gpio_num_t) this->scl_) == 0) {
    if (micros() - start > STRETCH_TIMEOUT_US)
      return false;
  }
  return true;
}

bool HatchExpander::bb_clear_() {
  this->bb_sda_(true);
  this->bb_scl_(true);
  this->bb_delay_();
  // A slave stuck mid-byte holds SDA low; clock it out (at most 9 bits), then send STOP.
  for (int i = 0; i < 9 && gpio_get_level((gpio_num_t) this->sda_) == 0; i++) {
    this->bb_scl_(false);
    this->bb_delay_();
    this->bb_scl_(true);
    this->bb_delay_();
  }
  this->bb_stop_();
  return gpio_get_level((gpio_num_t) this->sda_) && gpio_get_level((gpio_num_t) this->scl_);
}

bool HatchExpander::bb_start_() {
  // Bus must be idle: both lines released and high.
  this->bb_sda_(true);
  if (!this->bb_scl_(true) || !gpio_get_level((gpio_num_t) this->sda_))
    return false;
  this->bb_delay_();
  this->bb_sda_(false);
  this->bb_delay_();
  this->bb_scl_(false);
  this->bb_delay_();
  return true;
}

bool HatchExpander::bb_stop_() {
  this->bb_sda_(false);
  this->bb_delay_();
  bool ok = this->bb_scl_(true);
  this->bb_delay_();
  this->bb_sda_(true);
  this->bb_delay_();
  return ok && gpio_get_level((gpio_num_t) this->sda_);
}

bool HatchExpander::bb_write_byte_(uint8_t byte) {
  for (int i = 7; i >= 0; i--) {
    this->bb_sda_((byte >> i) & 1);
    this->bb_delay_();
    if (!this->bb_scl_(true))
      return false;
    this->bb_delay_();
    this->bb_scl_(false);
  }
  // ACK bit: release SDA, slave pulls it low.
  this->bb_sda_(true);
  this->bb_delay_();
  if (!this->bb_scl_(true))
    return false;
  this->bb_delay_();
  bool ack = gpio_get_level((gpio_num_t) this->sda_) == 0;
  this->bb_scl_(false);
  this->bb_delay_();
  return ack;
}

bool HatchExpander::bb_read_byte_(uint8_t *byte, bool ack) {
  uint8_t value = 0;
  this->bb_sda_(true);
  for (int i = 0; i < 8; i++) {
    this->bb_delay_();
    if (!this->bb_scl_(true))
      return false;
    this->bb_delay_();
    value = (value << 1) | (gpio_get_level((gpio_num_t) this->sda_) ? 1 : 0);
    this->bb_scl_(false);
  }
  this->bb_sda_(!ack);
  this->bb_delay_();
  if (!this->bb_scl_(true))
    return false;
  this->bb_delay_();
  this->bb_scl_(false);
  this->bb_sda_(true);
  *byte = value;
  return true;
}

bool HatchExpander::bb_write_reg_(uint8_t reg, uint8_t value) {
  if (!this->bb_start_()) {
    this->bb_fail_("start (bus busy)");
    return false;
  }
  if (!this->bb_write_byte_(this->address_ << 1) || !this->bb_write_byte_(reg) || !this->bb_write_byte_(value)) {
    this->bb_fail_("write NACK");
    return false;
  }
  return this->bb_stop_();
}

bool HatchExpander::bb_read_regs_(uint8_t reg, uint8_t *data, size_t len) {
  if (!this->bb_start_()) {
    this->bb_fail_("start (bus busy)");
    return false;
  }
  if (!this->bb_write_byte_(this->address_ << 1) || !this->bb_write_byte_(reg)) {
    this->bb_fail_("read: register NACK");
    return false;
  }
  // Repeated START, then read.
  if (!this->bb_start_() || !this->bb_write_byte_((this->address_ << 1) | 1)) {
    this->bb_fail_("read: address NACK");
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    if (!this->bb_read_byte_(&data[i], i + 1 < len)) {
      this->bb_fail_("read: SCL stuck");
      return false;
    }
  }
  return this->bb_stop_();
}

void HatchExpander::bb_fail_(const char *what) {
  this->bus_errors_++;
  ESP_LOGV(TAG, "Bus error: %s", what);
  // Leave the bus idle for the next attempt.
  this->bb_clear_();
}

size_t HatchExpanderPin::dump_summary(char *buffer, size_t len) const {
  return buf_append_printf(buffer, len, 0, "P%u_%u via hatch_expander", this->pin_ / 8, this->pin_ % 8);
}

}  // namespace esphome::hatch_expander
