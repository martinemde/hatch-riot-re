#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "esphome/components/audio/audio.h"
#include "esphome/components/media_source/media_source.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/static_task.h"

#include <freertos/FreeRTOS.h>
#include <sd_protocol_types.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

namespace esphome::sd_cache {

struct PlaySession;

// Media source for http(s) URLs with an optional cache on the SD card. A cache hit is read from the card; a miss is
// streamed from the web and copied to the card while it plays, then kept only if the download completed. With
// caching off (or no card) it just streams. Either way mono is sent as stereo and slow downloads are pre-buffered.
//
// Threading: the public API, play_uri(), handle_command() and loop() run on the main loop. The card is mounted and
// each playback runs in its own task; they report back through atomics that loop() picks up.
class SdCacheMediaSource final : public Component, public media_source::MediaSource {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Unmount before an OTA/restart, so no file is left open.
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_power_pin(GPIOPin *pin) { this->power_pin_ = pin; }
  void set_directory(const std::string &dir) { this->directory_ = dir; }
  void set_max_size_mb(uint32_t mb) { this->max_size_ = uint64_t(mb) << 20; }
  void set_min_free_mb(uint32_t mb) { this->min_free_ = uint64_t(mb) << 20; }
  void set_max_file_mb(uint32_t mb) { this->max_file_ = uint64_t(mb) << 20; }
  void add_exclude(const std::string &pattern) { this->excludes_.push_back(pattern); }
  void add_ignore_query_param(const std::string &param) { this->ignore_params_.push_back(param); }

  // The toggle. Enabling powers and mounts the card; disabling stops taking URLs, then unmounts and powers the card
  // off once nothing is playing from it. Main loop only.
  void set_enabled(bool enabled);
  bool is_enabled() const { return this->enabled_; }
  bool is_mounted() const { return this->mounted_.load(); }
  // Delete every cached file (only while idle). Main loop only.
  void clear_cache();
  uint32_t cache_files() const { return this->cache_files_.load(); }
  float cache_used_mb() const { return float(this->cache_bytes_.load()) / (1 << 20); }
  uint32_t hits() const { return this->hits_; }
  uint32_t misses() const { return this->misses_; }

  // MediaSource
  bool play_uri(const std::string &uri) override;
  void handle_command(media_source::MediaSourceCommand command) override;
  bool can_handle(const std::string &uri) const override;

 protected:
  static void mount_task_(void *arg);
  void mount_();
  void unmount_();
  static void play_task_(void *arg);
  void play_();
  // One pass through the file. `again`: a repeat pass, so don't count or log it as a new play. True if it reached the
  // end (not stopped, not failed).
  bool play_once_(bool again);
  static void fetch_task_(void *arg);  // arg: PlaySession *
  static void write_task_(void *arg);  // arg: this

  // Cache bookkeeping. Called with the card mounted and no other task touching the cache directory.
  std::string cache_key_(const std::string &url) const;
  bool excluded_(const std::string &url) const;
  void scan_cache_();
  // Delete least recently played files until the cache fits max_size_ and the card has `need` bytes on top of
  // min_free_. `keep` is never deleted. False if that can't be reached.
  bool make_room_(uint64_t need, const std::string &keep);
  uint64_t card_free_();

  GPIOPin *power_pin_{nullptr};
  std::string directory_;
  std::string dir_path_;  // "/sdcard/<directory_>"
  uint64_t max_size_{400ull << 20};
  uint64_t min_free_{50ull << 20};
  uint64_t max_file_{100ull << 20};
  std::vector<std::string> excludes_;
  std::vector<std::string> ignore_params_;

  bool enabled_{false};
  bool powered_{false};
  sdmmc_card_t *card_{nullptr};
  std::atomic<bool> mounted_{false};
  std::atomic<bool> mounting_{false};
  // Set by the mount task on failure; cleared when caching is enabled again.
  std::atomic<bool> mount_failed_{false};

  std::atomic<uint64_t> cache_bytes_{0};
  // Card free space: read once at mount (a FAT scan can take seconds), then tracked as files come and go.
  std::atomic<uint64_t> free_bytes_{0};
  std::atomic<uint32_t> cache_files_{0};
  uint32_t hits_{0};
  uint32_t misses_{0};

  // Current playback, owned by the play task while play_running_ is set.
  std::string play_url_;
  bool play_caching_{false};  // caching on and card mounted when this playback started
  std::atomic<bool> play_running_{false};
  std::atomic<bool> play_failed_{false};
  std::atomic<bool> play_hit_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> paused_{false};
  // Repeat one: the play task starts the file again at the end instead of finishing. The speaker keeps running and
  // the decoder runs ahead of it, so the next pass starts while the last one is still playing: no gap.
  std::atomic<bool> repeat_{false};
  audio::AudioStreamInfo stream_info_;
  // Playback task stacks live in PSRAM: internal RAM is needed for the I2S DMA buffers, and with three internal
  // stacks the speaker failed to start. A finished task parks itself; its owner deletes it.
  StaticTask play_task_mem_;
  StaticTask fetch_task_mem_;
  StaticTask write_task_mem_;
  PlaySession *session_{nullptr};  // the running playback, for the card writer
  bool reported_playing_{false};
  bool error_pending_{false};  // ERROR was reported; report IDLE on the next loop
};

}  // namespace esphome::sd_cache

#endif  // USE_ESP32
