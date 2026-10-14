#pragma once

#include <atomic>
#include <string>

#include "esphome/components/speaker/speaker.h"

#include "ff.h"
#include "esphome/core/component.h"

#include "bitbang_i2c.h"

namespace esphome {
namespace riot_probe {

// 7-segment glyph for an ASCII character (stock font table at 0x3f486910, bit7 = DP).
uint8_t seg_encode(char c);
// Brightness byte for display level 0..15 (stock table at 0x3f432e0c).
uint8_t seg_brightness(uint8_t level);

class RiotProbe : public Component {
 public:
  // Run before the I2C buses are set up so that variant detection sees the raw pins, as the stock
  // firmware does.
  float get_setup_priority() const override { return setup_priority::BUS + 10.0f; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Bit-banged bus (see bitbang_i2c.h). `address` is the expander's.
  void set_bus(uint8_t sda, uint8_t scl, uint32_t frequency, uint8_t address) {
    this->sda_pin_ = sda;
    this->scl_pin_ = scl;
    this->frequency_ = frequency;
    this->address_ = address;
  }
  // Release the bus before an OTA/restart reboot, so no chip is left mid-transaction.
  void on_shutdown() override;
  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  void set_write_timeout(uint32_t ms) { this->write_timeout_ = ms; }
  // Set the button pins P0_0-P0_4 to INPUT once at boot, as the stock firmware does, without arming.
  void set_buttons_at_boot(bool on) { this->buttons_at_boot_ = on; }

  // --- read-only API ---
  const std::string &variant() const { return this->variant_; }
  uint8_t chip_id() const { return this->chip_id_; }
  bool expander_ok() const { return this->snapshot_ok_; }
  uint16_t inputs() const { return this->inputs_; }
  // Input bits seen changing since boot. These pins are treated as inputs and never driven.
  uint16_t seen_toggling() const { return this->seen_toggling_; }
  uint32_t change_count() const { return this->change_count_; }
  uint16_t driven_mask() const { return this->driven_mask_; }
  bool armed() const { return this->armed_; }
  bool p0_push_pull_modified() const { return this->gcr_modified_; }
  bool inputs_mode() const { return this->cfg_modified_; }
  // Log `count` registers from `start` of the device at `address` (same bus). Read only.
  void dump(uint8_t address, uint8_t start, uint8_t count);
  void dump_expander();

  // --- guarded write API (expander @0x58 only) ---
  void arm(bool armed);
  // Drive one expander pin (0..15, P0_0..P1_7) to `high`. Reverts after write_timeout. P0_6, P0_7 and
  // P1_4-P1_7 are refused (the stock firmware never changes them).
  bool drive_pin(uint8_t pin, bool high);
  // Set AW9523B P0 to push-pull (GCR bit 4), as the stock driver does. Reverted with everything else.
  bool set_p0_push_pull(bool push_pull);
  // Put the button pins P0_0-P0_4 into INPUT mode (CFG0 |= 0x1F, the stock config) so presses can be read.
  // Auto-reverts to the boot snapshot like any other guarded write. `on=false` reverts immediately.
  bool set_button_inputs(bool on);
  // Restore OUT/CFG/GCR registers to the values read at boot.
  void restore();

  // --- Accelerometer (DA213 @0x27) ---
  // Apply the stock DA213 setup (the probe's only accelerometer writes), then log X/Y/Z once a second.
  void accel_setup();
  bool accel_ready() const { return this->accel_ready_; }
  int16_t accel_axis(int i) const { return this->accel_[i]; }
  // Stock "is upright": Z < -0x7FF counts (about -0.5 g).
  bool accel_upright() const { return this->accel_[2] < -0x7FF; }

  // --- Status LEDs (RIoT) ---
  // window 0 = Wi-Fi (GPIO13), 1 = lock (GPIO32); state 0 off, 1 green, 2 red, 3 amber (stock encoding).
  void set_status_indicator(uint8_t window, uint8_t state);

  // --- SD card (read only) ---
  // Initialise the card on the stock slot (SDMMC slot 1, 1-bit: CLK 14, CMD 15, D0 2, internal pull-ups) and log
  // its ID/size. Reads card registers only: no filesystem mount, no data reads or writes. Needs SD power
  // (P1_1 high, then ~1 s to settle, as stock does).
  void probe_sd();
  // Mount the card's FAT volume read-only and log the directory tree (sizes, dates, WAV formats, small .txt
  // contents) and free space. Powers the card on P1_1 first, so it needs arm and auto-reverts like drive_pin.
  // Writes are refused at the disk layer: the volume reports write-protected and the write hook fails.
  // Runs in its own task. `root` is the folder to start from ("/" = card root); `max_depth` 1 = that folder only;
  // `max_entries` caps the logged files (totals still count every file).
  void list_sd(const std::string &root, uint8_t max_depth, uint16_t max_entries);
  // Play a file from the card through `spk`, read-only like list_sd: 16-bit PCM WAV (mono or stereo; stereo mixed
  // down when `mix_to_mono`), or MP3 (decoded, always sent as stereo). Powers
  // the card, starts the speaker, then switches the amp (P1_2) on once I2S runs. Plays at most `max_seconds`, capped
  // to fit the write_timeout window, then reverts both pins.
  void play_sd_file(speaker::Speaker *spk, const std::string &path, float volume, uint16_t max_seconds,
                    bool mix_to_mono = true);

 protected:
  // Same shape as the I2CDevice calls they replace, on the bit-banged bus.
  i2c::ErrorCode read_register(uint8_t reg, uint8_t *data, size_t len) {
    return this->bus_.read_regs(this->address_, reg, data, len);
  }
  i2c::ErrorCode write_register(uint8_t reg, const uint8_t *data, size_t len) {
    return this->bus_.write_regs(this->address_, reg, data, len);
  }
  void set_i2c_address(uint8_t address) { this->address_ = address; }

  // Power the card, then start `task` 1 s later (stock settle time). False if busy or the power write failed.
  bool sd_start_task_(const char *name, TaskFunction_t task);
  static void sd_list_task_(void *arg);
  static void sd_play_task_(void *arg);
  void sd_play_();
  void sd_play_mp3_(FIL *f);
  void sd_list_();
  void detect_variant_();
  bool snapshot_();
  bool read_inputs_(uint16_t *value);
  bool guarded_write_(uint8_t reg, uint8_t value);
  // Write one expander register, retrying once on failure (the bus is flaky). Logs the I2C error code.
  bool write_reg_(uint8_t reg, uint8_t value);
  bool read_reg_(uint8_t reg, uint8_t *value);
  // Block until the post-write quiet period has passed.
  void settle_();
  void accel_read_();
  void status_leds_setup_();
  // I2C health counters for expander traffic, logged every 30 s.
  void count_(bool write, i2c::ErrorCode err);
  void log_i2c_stats_();

  static constexpr int ERR_SLOTS = 8;
  uint32_t win_reads_{0};
  uint32_t win_writes_{0};
  uint32_t win_errors_[ERR_SLOTS]{};
  uint32_t total_ops_{0};
  uint32_t total_errors_{0};
  BitBangI2C bus_;
  uint8_t sda_pin_{0};
  uint8_t scl_pin_{22};
  uint32_t frequency_{50000};
  uint8_t address_{0x58};
  bool buttons_at_boot_{false};
  bool accel_ready_{false};
  bool status_ok_{false};
  int16_t accel_[3]{0, 0, 0};

  std::atomic<bool> sd_busy_{false};  // a probe or listing owns the SDMMC host
  std::string sd_root_;
  speaker::Speaker *sd_speaker_{nullptr};
  std::string sd_play_path_;
  bool sd_mix_to_mono_{true};
  uint16_t sd_play_seconds_{20};
  uint8_t sd_max_depth_{3};
  uint16_t sd_max_entries_{400};

  uint32_t poll_interval_{50};
  uint32_t fallback_interval_{1000};  // idle read rate when GPIO34 INT is high
  uint32_t quiet_until_{0};           // no expander transaction before this millis()
  uint32_t write_timeout_{30000};
  uint32_t last_poll_{0};
  uint32_t ready_at_{0};
  uint32_t fail_streak_{0};  // consecutive I2C failures; drives poll back-off

  std::string variant_{"unknown"};
  uint8_t sense_[3]{0, 0, 0};

  bool snapshot_tried_{false};
  bool snapshot_ok_{false};
  uint8_t chip_id_{0};
  uint8_t snap_out_[2]{0, 0};
  uint8_t snap_cfg_[2]{0xFF, 0xFF};
  uint8_t snap_gcr_{0};

  bool have_inputs_{false};
  uint16_t inputs_{0};
  uint16_t seen_toggling_{0};
  uint32_t change_count_{0};
  uint16_t candidate_value_{0};    // debounce: value awaiting a confirming second read
  uint8_t candidate_streak_{0};    // consecutive polls the candidate has held

  bool armed_{false};
  uint16_t driven_mask_{0};
  bool gcr_modified_{false};
  bool cfg_modified_{false};
};

}  // namespace riot_probe
}  // namespace esphome
