#include "hatch_expander.h"

#include "esphome/core/log.h"

namespace esphome::hatch_expander {

static const char *const TAG = "hatch_expander";

static const uint8_t REG_IN = 0x00, REG_OUT = 0x02, REG_CFG = 0x04, REG_INT = 0x06, REG_ID = 0x10, REG_GCR = 0x11;
static const uint8_t CHIP_ID = 0x23;
static const uint8_t GCR_P0_PUSH_PULL = 0x10;
// Bits the stock firmware's pin model covers, per port; everything else is left at its power-on state.
static const uint8_t STOCK_MASK[2] = {0x3F, 0x0F};
// Switching a load (LED driver, amp) makes the chip NACK for ~10-25 ms; a transaction in that window can also hang
// the IDF I2C driver. Keep every transaction out of it, and space retries.
static const uint32_t SETTLE_MS = 20, RETRY_GAP_MS = 10;
static const int WRITE_ATTEMPTS = 3;
// Without an interrupt pin, don't read more often than this (binary sensors poll every loop).
static const uint32_t MIN_POLL_MS = 50;
// With an interrupt pin, still re-read occasionally in case an edge was missed.
static const uint32_t FALLBACK_REFRESH_MS = 1000;

void HatchExpander::setup() {
  // The bus is unreliable for the first ~2 s after boot; retry the ID read before giving up.
  uint8_t id = 0;
  bool found = false;
  for (int i = 0; i < 20 && !found; i++) {
    found = this->read_regs_(REG_ID, &id, 1) && id == CHIP_ID;
    if (!found)
      delay(100);
  }
  if (!found) {
    ESP_LOGE(TAG, "Expander not found at 0x%02X (ID 0x%02X, expected 0x%02X)", this->address_, id, CHIP_ID);
    this->mark_failed();
    return;
  }
  uint8_t gcr = 0, ints[2] = {0xFF, 0xFF};
  if (!this->read_regs_(REG_OUT, this->out_, 2) || !this->read_regs_(REG_CFG, this->cfg_, 2) ||
      !this->read_regs_(REG_INT, ints, 2) || !this->read_regs_(REG_GCR, &gcr, 1)) {
    ESP_LOGE(TAG, "Could not read expander state");
    this->mark_failed();
    return;
  }
  // Interrupts off for every stock pin until a pin is configured as an input.
  this->update_reg_(REG_INT, STOCK_MASK[0], STOCK_MASK[0]);
  this->update_reg_(REG_INT + 1, STOCK_MASK[1], STOCK_MASK[1]);
  uint8_t want = this->p0_push_pull_ ? (gcr | GCR_P0_PUSH_PULL) : (gcr & ~GCR_P0_PUSH_PULL);
  if (want != gcr)
    this->write_reg_(REG_GCR, want);
  this->ready_ = true;

  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->interrupt_pin_->attach_interrupt(&HatchExpander::gpio_intr, this, gpio::INTERRUPT_FALLING_EDGE);
    this->set_invalidate_on_read_(false);
    this->set_interval("refresh", FALLBACK_REFRESH_MS, [this]() { this->enable_loop(); });
  }
  this->disable_loop();
}

void IRAM_ATTR HatchExpander::gpio_intr(HatchExpander *arg) { arg->enable_loop_soon_any_context(); }

void HatchExpander::loop() {
  this->reset_pin_cache_();
  // INT stays asserted until the inputs are read; keep looping until it's released so no edge is stranded.
  if (this->interrupt_pin_ == nullptr || this->interrupt_pin_->digital_read())
    this->disable_loop();
}

void HatchExpander::dump_config() {
  ESP_LOGCONFIG(TAG, "Hatch expander (stock AW9110B pin model):");
  LOG_I2C_DEVICE(this);
  LOG_PIN("  Interrupt pin: ", this->interrupt_pin_);
  ESP_LOGCONFIG(TAG, "  P0 push-pull: %s\n  Boot state: OUT=%02X %02X CFG=%02X %02X", YESNO(this->p0_push_pull_),
                this->out_[1], this->out_[0], this->cfg_[1], this->cfg_[0]);
  if (this->is_failed())
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
}

bool HatchExpander::digital_read_hw(uint8_t pin) {
  if (!this->ready_)
    return false;
  if (this->interrupt_pin_ == nullptr && millis() - this->last_read_ < MIN_POLL_MS)
    return true;  // keep the cached value
  this->last_read_ = millis();
  uint8_t in[2];
  if (!this->read_regs_(REG_IN, in, 2)) {
    this->status_set_warning();
    return false;
  }
  this->status_clear_warning();
  this->inputs_ = in[0] | (uint16_t(in[1]) << 8);
  return true;
}

void HatchExpander::digital_write_hw(uint8_t pin, bool value) {
  if (!this->ready_)
    return;
  uint8_t port = pin / 8, bit = 1u << (pin % 8);
  this->update_reg_(REG_OUT + port, bit, value ? bit : 0);
}

void HatchExpander::pin_mode(uint8_t pin, gpio::Flags flags) {
  if (!this->ready_)
    return;
  uint8_t port = pin / 8, bit = 1u << (pin % 8);
  bool input = flags & gpio::FLAG_INPUT;
  this->update_reg_(REG_CFG + port, bit, input ? bit : 0);  // 1 = input
  this->update_reg_(REG_INT + port, bit, input ? 0 : bit);  // 0 = interrupt enabled
  if (input) {
    this->input_pins_ |= 1u << pin;
    this->enable_loop();
  }
}

bool HatchExpander::update_reg_(uint8_t reg, uint8_t mask, uint8_t bits) {
  uint8_t port = reg & 1;
  mask &= STOCK_MASK[port];
  uint8_t *cache = nullptr;
  if ((reg & ~1) == REG_OUT)
    cache = &this->out_[port];
  else if ((reg & ~1) == REG_CFG)
    cache = &this->cfg_[port];
  uint8_t cur;
  if (cache != nullptr) {
    cur = *cache;
  } else if (!this->read_regs_(reg, &cur, 1)) {
    return false;
  }
  uint8_t value = (cur & ~mask) | (bits & mask);
  if (value == cur)
    return true;
  if (!this->write_reg_(reg, value))
    return false;
  if (cache != nullptr)
    *cache = value;
  return true;
}

bool HatchExpander::write_reg_(uint8_t reg, uint8_t value) {
  for (int attempt = 1; attempt <= WRITE_ATTEMPTS; attempt++) {
    this->settle_();
    i2c::ErrorCode err = this->write_register(reg, &value, 1);
    this->quiet_until_ = millis() + (err == i2c::ERROR_OK ? SETTLE_MS : RETRY_GAP_MS);
    if (err == i2c::ERROR_OK)
      return true;
    ESP_LOGW(TAG, "Write reg 0x%02X <- 0x%02X failed (attempt %d/%d, i2c error %d)", reg, value, attempt,
             WRITE_ATTEMPTS, (int) err);
  }
  this->status_set_warning();
  return false;
}

bool HatchExpander::read_regs_(uint8_t reg, uint8_t *data, size_t len) {
  this->settle_();
  return this->read_register(reg, data, len) == i2c::ERROR_OK;
}

void HatchExpander::settle_() {
  int32_t wait = int32_t(this->quiet_until_ - millis());
  if (wait > 0)
    delay(wait);
}

size_t HatchExpanderPin::dump_summary(char *buffer, size_t len) const {
  return buf_append_printf(buffer, len, 0, "P%u_%u via hatch_expander", this->pin_ / 8, this->pin_ % 8);
}

}  // namespace esphome::hatch_expander
