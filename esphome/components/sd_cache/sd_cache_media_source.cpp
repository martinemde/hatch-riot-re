#include "sd_cache_media_source.h"

#ifdef USE_ESP32

#include "esphome/components/audio/audio_decoder.h"
#include "esphome/components/ring_buffer/ring_buffer.h"
#include "esphome/core/log.h"

#include <dirent.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>

namespace esphome::sd_cache {

static const char *const TAG = "sd_cache";

static constexpr const char *BASE_PATH = "/sdcard";
static constexpr const char *PART_SUFFIX = ".part";
// Plays a cached file by name (sdcache://<key>.<ext>), for cycling through the cache from the device's buttons.
static constexpr const char *LOCAL_SCHEME = "sdcache://";
// The stock firmware waits 1 s after switching the card on (hatch_sd_init 0x4010e6a4).
static constexpr uint32_t POWER_SETTLE_MS = 1000;
static constexpr size_t CHUNK = 4096;
static constexpr size_t WRITE_CHUNK = 8192;
// Internal RAM is tight once the speaker starts (it failed to with an 8 KB buffer here), so keep this small.
static constexpr size_t FETCH_CHUNK = 4096;
static constexpr size_t HEADER_BYTES = 4096;
// All in PSRAM. The input buffer covers ~1.5 s of 44.1 kHz stereo WAV against network jitter; the card buffer
// covers ~3 s of card write stalls (up to 121 ms seen) before caching is abandoned.
static constexpr size_t DECODER_INPUT_BUFFER = 256 * 1024;
static constexpr size_t CACHE_WRITE_BUFFER = 512 * 1024;
// Start decoding once this much is buffered (or the file has all arrived), waiting at most PREBUFFER_MAX_MS. On a
// miss over a link slower than the audio's data rate, more is buffered first (see play_()), up to MAX_INPUT_BUFFER.
static constexpr size_t PREBUFFER = 64 * 1024;
static constexpr uint32_t PREBUFFER_MAX_MS = 1500;
static constexpr size_t MAX_INPUT_BUFFER = 2 * 1024 * 1024;
// Measure the download for this long before trusting the rate (the first second is a fast burst here).
static constexpr uint32_t RATE_SAMPLE_MS = 3000;
// During a miss, pause to rebuffer when the decoder's input drops below this.
static constexpr size_t REBUFFER_LOW_WATER = 32 * 1024;
static constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
static constexpr int MAX_REDIRECTS = 5;
// Give up on a download after this long without data (single reads time out after HTTP_TIMEOUT_MS).
static constexpr uint32_t STALL_GIVE_UP_MS = 30000;
// The play task connects (TLS handshake) and decodes; the fetch task reads TLS records.
static constexpr uint32_t PLAY_TASK_STACK = 10240;
static constexpr uint32_t FETCH_TASK_STACK = 8192;
static constexpr uint32_t WRITE_TASK_STACK = 4096;
static constexpr uint32_t MOUNT_TASK_STACK = 6144;
static constexpr UBaseType_t TASK_PRIORITY = 2;
// The download and card writer run below the decoder, at priority 1 (ESPHome's audio tasks are 1-2), so a busy
// download never takes CPU from the audio chain.
static constexpr UBaseType_t HELPER_TASK_PRIORITY = 1;
static constexpr uint32_t PAUSE_POLL_MS = 20;

namespace {

struct FileTypeExt {
  audio::AudioFileType type;
  const char *ext;
};

const FileTypeExt FILE_TYPES[] = {
#ifdef USE_AUDIO_MP3_SUPPORT
    {audio::AudioFileType::MP3, "mp3"},
#endif
#ifdef USE_AUDIO_FLAC_SUPPORT
    {audio::AudioFileType::FLAC, "flac"},
#endif
#ifdef USE_AUDIO_WAV_SUPPORT
    {audio::AudioFileType::WAV, "wav"},
#endif
};

// The type of a cached file, from its extension; NONE if it isn't one.
audio::AudioFileType type_for(const char *name) {
  const char *dot = strrchr(name, '.');
  if (dot != nullptr) {
    for (const auto &ft : FILE_TYPES) {
      if (strcmp(dot + 1, ft.ext) == 0)
        return ft.type;
    }
  }
  return audio::AudioFileType::NONE;
}

const char *ext_for(audio::AudioFileType type) {
  for (const auto &ft : FILE_TYPES) {
    if (ft.type == type)
      return ft.ext;
  }
  return nullptr;
}

bool ends_with(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Decoded audio goes to the orchestrator; held back while paused, dropped once a stop is requested. Mono is sent
// as stereo (each sample in both channels): 16-bit mono at 44.1 kHz plays at half speed on this I2S speaker, and the
// resampler that would avoid it can't keep up while Wi-Fi is busy.
class OutputSink : public audio::AudioSinkCallback {
 public:
  OutputSink(media_source::MediaSource *source, audio::AudioDecoder *decoder, std::atomic<bool> *paused,
             std::atomic<bool> *stop)
      : source_(source), decoder_(decoder), paused_(paused), stop_(stop) {}
  ~OutputSink() { heap_caps_free(this->stereo_); }

  size_t audio_sink_write(uint8_t *data, size_t length, TickType_t ticks_to_wait) override {
    if (this->stop_->load())
      return length;
    if (this->paused_->load()) {
      vTaskDelay(pdMS_TO_TICKS(PAUSE_POLL_MS));
      return 0;
    }
    const auto &info = this->decoder_->get_audio_stream_info();
    if (!info.has_value())
      return 0;
    if (info->get_channels() != 1)
      return this->source_->write_output(data, length, pdTICKS_TO_MS(ticks_to_wait), info.value());

    // Mono: duplicate into a stereo buffer and hand all of it over, so the input consumed is whole samples.
    if (this->stereo_ == nullptr) {
      this->stereo_ = static_cast<uint8_t *>(heap_caps_malloc(2 * STEREO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      if (this->stereo_ == nullptr)
        return 0;
    }
    const size_t bps = info->get_bits_per_sample() / 8;
    const size_t in = std::min(length, STEREO_CHUNK) / bps * bps;
    for (size_t i = 0; i < in; i += bps) {
      memcpy(this->stereo_ + 2 * i, data + i, bps);
      memcpy(this->stereo_ + 2 * i + bps, data + i, bps);
    }
    const audio::AudioStreamInfo stereo(info->get_bits_per_sample(), 2, info->get_sample_rate());
    size_t off = 0;
    while (off < 2 * in && !this->stop_->load())
      off += this->source_->write_output(this->stereo_ + off, 2 * in - off, pdTICKS_TO_MS(ticks_to_wait), stereo);
    return in;
  }

 protected:
  static constexpr size_t STEREO_CHUNK = 2048;  // input bytes per pass
  media_source::MediaSource *source_;
  audio::AudioDecoder *decoder_;
  std::atomic<bool> *paused_;
  std::atomic<bool> *stop_;
  uint8_t *stereo_{nullptr};
};

}  // namespace

void SdCacheMediaSource::setup() {
  this->dir_path_ = std::string(BASE_PATH) + "/" + this->directory_;
  this->power_pin_->setup();
  this->power_pin_->digital_write(false);
}

void SdCacheMediaSource::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SD Cache Media Source:\n"
                "  Directory: %s\n"
                "  Max size: %" PRIu32 " MB, keep free: %" PRIu32 " MB, max file: %" PRIu32 " MB\n"
                "  Enabled: %s, mounted: %s",
                this->dir_path_.c_str(), uint32_t(this->max_size_ >> 20), uint32_t(this->min_free_ >> 20),
                uint32_t(this->max_file_ >> 20), YESNO(this->enabled_), YESNO(this->mounted_.load()));
  LOG_PIN("  Power pin: ", this->power_pin_);
  for (const auto &e : this->excludes_)
    ESP_LOGCONFIG(TAG, "  Never cached: URLs containing '%s'", e.c_str());
}

// ---------------------------------------------------------------- enable / mount

void SdCacheMediaSource::set_enabled(bool enabled) {
  if (enabled == this->enabled_)
    return;
  this->enabled_ = enabled;
  ESP_LOGI(TAG, "Caching %s", enabled ? "enabled" : "disabled");
  if (enabled)
    this->mount_failed_ = false;
  this->enable_loop();  // loop() mounts or unmounts the card to match
}

void SdCacheMediaSource::loop() {
  // A playback task finished: report it. ERROR first if it failed, then IDLE on the next pass.
  if (this->reported_playing_ && !this->play_running_.load()) {
    this->play_task_mem_.destroy();  // retried by play_uri() if it was still being swapped out
    this->reported_playing_ = false;
    if (this->play_failed_.load()) {
      this->set_state_(media_source::MediaSourceState::ERROR);
      this->error_pending_ = true;
      return;
    }
    this->set_state_(media_source::MediaSourceState::IDLE);
  }
  if (this->error_pending_) {
    this->error_pending_ = false;
    this->set_state_(media_source::MediaSourceState::IDLE);
  }

  // Bring the card in line with the toggle.
  if (this->enabled_ && !this->mounted_.load() && !this->mounting_.load() && !this->mount_failed_ &&
      !this->powered_) {
    this->powered_ = true;
    this->power_pin_->digital_write(true);
    this->mounting_ = true;
    this->set_timeout("mount", POWER_SETTLE_MS, [this]() {
      if (xTaskCreate(SdCacheMediaSource::mount_task_, "sd_mount", MOUNT_TASK_STACK, this, TASK_PRIORITY, nullptr) !=
          pdPASS) {
        ESP_LOGE(TAG, "Could not start the mount task");
        this->mounting_ = false;
        this->mount_failed_ = true;
      }
    });
  }
  if (!this->enabled_ && this->mounted_.load() && !this->play_running_.load())
    this->unmount_();
  if (this->powered_ && !this->mounted_.load() && !this->mounting_.load() &&
      (!this->enabled_ || this->mount_failed_)) {
    this->powered_ = false;
    this->power_pin_->digital_write(false);
  }

  // Nothing left to do until the toggle changes or a playback starts.
  bool settled = (this->enabled_ && this->mounted_.load() && this->powered_) ||
                 (!this->enabled_ && !this->mounted_.load() && !this->powered_) ||
                 (this->mount_failed_.load() && !this->powered_);
  if (settled && !this->play_running_.load() && !this->mounting_.load() && !this->error_pending_)
    this->disable_loop();
}

void SdCacheMediaSource::mount_task_(void *arg) {
  auto *self = static_cast<SdCacheMediaSource *>(arg);
  self->mount_();
  self->mounting_ = false;
  self->enable_loop_soon_any_context();
  vTaskDelete(nullptr);
}

void SdCacheMediaSource::mount_() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;  // 20 MHz default; the probe read ~650 KB/s like this
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;  // never format the stock card
  cfg.max_files = 3;
  cfg.allocation_unit_size = 0;
  esp_err_t err = esp_vfs_fat_sdmmc_mount(BASE_PATH, &host, &slot, &cfg, &this->card_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mount failed: %s; caching stays off until it is toggled again", esp_err_to_name(err));
    this->card_ = nullptr;
    this->mount_failed_ = true;
    return;
  }
  struct stat st;
  if (stat(this->dir_path_.c_str(), &st) != 0 && mkdir(this->dir_path_.c_str(), 0775) != 0) {
    ESP_LOGE(TAG, "Cannot create %s", this->dir_path_.c_str());
    esp_vfs_fat_sdcard_unmount(BASE_PATH, this->card_);
    this->card_ = nullptr;
    this->mount_failed_ = true;
    return;
  }
  this->free_bytes_ = this->card_free_();
  this->scan_cache_();
  this->mounted_ = true;
  ESP_LOGI(TAG, "Card mounted: cache %" PRIu32 " files, %.1f MB; card free %" PRIu32 " MB", this->cache_files_.load(),
           this->cache_used_mb(), uint32_t(this->free_bytes_.load() >> 20));
}

void SdCacheMediaSource::unmount_() {
  if (!this->mounted_.load())
    return;
  esp_vfs_fat_sdcard_unmount(BASE_PATH, this->card_);
  this->card_ = nullptr;
  this->mounted_ = false;
  ESP_LOGI(TAG, "Card unmounted");
}

void SdCacheMediaSource::on_shutdown() {
  this->stop_requested_ = true;
  for (int i = 0; i < 50 && this->play_running_.load(); i++)
    vTaskDelay(pdMS_TO_TICKS(20));
  if (!this->play_running_.load() && !this->mounting_.load())
    this->unmount_();
}

// ---------------------------------------------------------------- cache bookkeeping

bool SdCacheMediaSource::excluded_(const std::string &url) const {
  for (const auto &e : this->excludes_) {
    if (url.find(e) != std::string::npos)
      return true;
  }
  return false;
}

std::string SdCacheMediaSource::cache_key_(const std::string &url) const {
  // Drop ignored query parameters, then FNV-1a 64 of what is left.
  std::string norm = url;
  size_t q = url.find('?');
  if (q != std::string::npos) {
    norm = url.substr(0, q);
    std::string kept;
    size_t pos = q + 1;
    while (pos <= url.size()) {
      size_t amp = url.find('&', pos);
      if (amp == std::string::npos)
        amp = url.size();
      std::string param = url.substr(pos, amp - pos);
      std::string name = param.substr(0, param.find('='));
      bool ignore = param.empty();
      for (const auto &p : this->ignore_params_)
        ignore |= name == p;
      if (!ignore)
        kept += (kept.empty() ? "?" : "&") + param;
      pos = amp + 1;
    }
    norm += kept;
  }
  uint64_t h = 0xcbf29ce484222325ull;
  for (unsigned char c : norm) {
    h ^= c;
    h *= 0x100000001b3ull;
  }
  char buf[17];
  snprintf(buf, sizeof(buf), "%016" PRIx64, h);
  return buf;
}

uint64_t SdCacheMediaSource::card_free_() {
  uint64_t total = 0, free = 0;
  if (esp_vfs_fat_info(BASE_PATH, &total, &free) != ESP_OK)
    return 0;
  return free;
}

void SdCacheMediaSource::scan_cache_() {
  uint64_t bytes = 0;
  uint32_t files = 0;
  DIR *dir = opendir(this->dir_path_.c_str());
  if (dir == nullptr)
    return;
  while (struct dirent *e = readdir(dir)) {
    if (e->d_type == DT_DIR)
      continue;
    std::string path = this->dir_path_ + "/" + e->d_name;
    if (ends_with(e->d_name, PART_SUFFIX)) {
      // A download that never completed (power loss, crash).
      struct stat st;
      if (stat(path.c_str(), &st) == 0)
        this->free_bytes_ += st.st_size;
      unlink(path.c_str());
      ESP_LOGW(TAG, "Removed incomplete download %s", e->d_name);
      continue;
    }
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
      bytes += st.st_size;
      files++;
    }
  }
  closedir(dir);
  this->cache_bytes_ = bytes;
  this->cache_files_ = files;
}

bool SdCacheMediaSource::make_room_(uint64_t need, const std::string &keep) {
  while (this->cache_bytes_.load() > this->max_size_ || this->free_bytes_.load() < this->min_free_ + need) {
    // Evict the least recently played file (mtime is bumped on every hit).
    std::string oldest;
    time_t oldest_time = 0;
    off_t oldest_size = 0;
    DIR *dir = opendir(this->dir_path_.c_str());
    if (dir == nullptr)
      return false;
    while (struct dirent *e = readdir(dir)) {
      if (e->d_type == DT_DIR || ends_with(e->d_name, PART_SUFFIX))
        continue;
      std::string path = this->dir_path_ + "/" + e->d_name;
      if (path == keep)
        continue;
      struct stat st;
      if (stat(path.c_str(), &st) == 0 && (oldest.empty() || st.st_mtime < oldest_time)) {
        oldest = path;
        oldest_time = st.st_mtime;
        oldest_size = st.st_size;
      }
    }
    closedir(dir);
    if (oldest.empty() || unlink(oldest.c_str()) != 0)
      return false;
    this->cache_bytes_ -= oldest_size;
    this->cache_files_--;
    this->free_bytes_ += oldest_size;
    ESP_LOGI(TAG, "Evicted %s (%" PRIu32 " KB)", oldest.c_str() + this->dir_path_.size() + 1,
             uint32_t(oldest_size / 1024));
  }
  return true;
}

void SdCacheMediaSource::clear_cache() {
  if (!this->mounted_.load() || this->play_running_.load()) {
    ESP_LOGW(TAG, "Clear cache: card not mounted or a playback is running");
    return;
  }
  DIR *dir = opendir(this->dir_path_.c_str());
  if (dir == nullptr)
    return;
  uint32_t removed = 0;
  while (struct dirent *e = readdir(dir)) {
    if (e->d_type == DT_DIR)
      continue;
    std::string path = this->dir_path_ + "/" + e->d_name;
    if (unlink(path.c_str()) == 0)
      removed++;
  }
  closedir(dir);
  this->free_bytes_ = this->card_free_();
  this->cache_bytes_ = 0;
  this->cache_files_ = 0;
  ESP_LOGI(TAG, "Cache cleared: %" PRIu32 " files removed", removed);
}

std::string SdCacheMediaSource::cached_uri(int step) {
  if (!this->mounted_.load())
    return "";
  std::vector<std::string> names;
  DIR *dir = opendir(this->dir_path_.c_str());
  if (dir == nullptr)
    return "";
  while (struct dirent *e = readdir(dir)) {
    if (e->d_type != DT_DIR && type_for(e->d_name) != audio::AudioFileType::NONE)
      names.emplace_back(e->d_name);
  }
  closedir(dir);
  if (names.empty())
    return "";
  std::sort(names.begin(), names.end());
  // Step from the file playing now (or last played); if that isn't cached, start at the first (or last) file.
  const int n = int(names.size());
  int cur = -1;
  for (int i = 0; i < n && !this->last_played_.empty(); i++) {
    if (names[i].starts_with(this->last_played_)) {
      cur = i;
      break;
    }
  }
  const int next = cur < 0 ? (step >= 0 ? 0 : n - 1) : ((cur + step) % n + n) % n;
  return std::string(LOCAL_SCHEME) + names[next];
}

// ---------------------------------------------------------------- MediaSource

bool SdCacheMediaSource::can_handle(const std::string &uri) const {
  // Every web URL: with caching off (or no card) this source still streams, so mono gets sent as stereo.
  return uri.starts_with("http://") || uri.starts_with("https://") || uri.starts_with(LOCAL_SCHEME);
}

bool SdCacheMediaSource::play_uri(const std::string &uri) {
  if (!this->can_handle(uri) || !this->has_listener())
    return false;
  if (this->get_state() != media_source::MediaSourceState::IDLE || this->play_running_.load()) {
    ESP_LOGE(TAG, "Cannot play '%s': source is busy", uri.c_str());
    return false;
  }
  const bool local = uri.starts_with(LOCAL_SCHEME);
  if (local && !this->mounted_.load()) {
    ESP_LOGE(TAG, "Cannot play '%s': card not mounted", uri.c_str());
    return false;
  }
  this->last_played_ = local ? uri.substr(strlen(LOCAL_SCHEME)) : this->cache_key_(uri);
  this->play_url_ = uri;
  this->play_caching_ = this->enabled_ && this->mounted_.load();
  this->stop_requested_ = false;
  this->paused_ = false;
  this->play_failed_ = false;
  if (!this->play_task_mem_.destroy()) {  // the previous playback's task hasn't been swapped out yet
    ESP_LOGW(TAG, "Previous playback still winding down");
    return false;
  }
  this->play_running_ = true;
  if (!this->play_task_mem_.create(SdCacheMediaSource::play_task_, "sd_cache", PLAY_TASK_STACK, this, TASK_PRIORITY,
                                   true)) {
    ESP_LOGE(TAG, "Could not start the playback task");
    this->play_running_ = false;
    return false;
  }
  this->reported_playing_ = true;
  this->set_state_(media_source::MediaSourceState::PLAYING);
  this->enable_loop();
  return true;
}

void SdCacheMediaSource::handle_command(media_source::MediaSourceCommand command) {
  switch (command) {
    case media_source::MediaSourceCommand::STOP:
      this->stop_requested_ = true;
      break;
    case media_source::MediaSourceCommand::PAUSE:
      if (this->get_state() != media_source::MediaSourceState::PLAYING)
        break;
      this->set_state_(media_source::MediaSourceState::PAUSED);
      this->paused_ = true;
      break;
    case media_source::MediaSourceCommand::PLAY:
      if (this->get_state() != media_source::MediaSourceState::PAUSED)
        break;
      this->set_state_(media_source::MediaSourceState::PLAYING);
      this->paused_ = false;
      break;
    // From the player (speaker_source patch). Also applies to a playback already running.
    case media_source::MediaSourceCommand::REPEAT_ONE:
      this->repeat_ = true;
      break;
    case media_source::MediaSourceCommand::REPEAT_OFF:
    case media_source::MediaSourceCommand::REPEAT_ALL:
      this->repeat_ = false;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- playback

// One playback runs as three tasks, so the speaker is never starved by the network or the card:
//   play_task_  (this one): opens the source, then decodes from `input` to the orchestrator.
//   fetch_task_: web (esp_http_client) or cached file -> `input`, plus a copy into `cache_ring` on a miss.
//   write_task_: `cache_ring` -> the .part file on the card; renamed into place only if the download completed.
// The decoder only ever waits on its own input. If the card falls behind and `cache_ring` fills, caching of that
// file is abandoned instead of holding up the download.
struct PlaySession {
  std::string url;
  std::string part_path, final_path;
  FILE *src{nullptr};                   // cached file (hit)
  esp_http_client_handle_t http{nullptr};  // web (miss)
  std::string content_type;
  int64_t content_length{-1};  // -1: unknown (chunked or a live stream)
  audio::AudioFileType type{audio::AudioFileType::NONE};
  // Bytes per second of the *file* that playback consumes, from its header (0: unknown).
  std::atomic<uint32_t> byte_rate{0};
  std::shared_ptr<ring_buffer::RingBuffer> input;
  size_t input_size{0};
  std::shared_ptr<ring_buffer::RingBuffer> cache_ring;  // null when not caching
  FILE *cache_file{nullptr};
  uint64_t written{0};
  std::atomic<uint64_t> fetched{0};
  std::atomic<uint32_t> first_byte_ms{0};
  std::atomic<bool> abort{false};          // decoder finished, failed or was stopped: helpers wind down
  std::atomic<bool> fetch_done{false};
  std::atomic<bool> fetch_complete{false};  // the whole file arrived
  std::atomic<bool> fetch_running{false};
  std::atomic<bool> write_running{false};
  std::atomic<bool> cache_abandoned{false};
  const char *abandon_reason{nullptr};
  uint32_t max_read_ms{0}, max_write_ms{0};
};

// Bytes per second a file needs, from its first bytes. WAV states it; FLAC is estimated at ~65% of PCM; MP3 is
// taken as 320 kbit/s (the most it can be). 0 if unknown.
static uint32_t estimate_byte_rate(audio::AudioFileType type, const uint8_t *b, size_t n) {
  auto le32 = [](const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); };
  switch (type) {
#ifdef USE_AUDIO_WAV_SUPPORT
    case audio::AudioFileType::WAV: {
      // If the fmt chunk can't be found, assume CD audio (44.1 kHz stereo 16-bit): buffering too much beats
      // stuttering.
      static constexpr uint32_t CD_RATE = 44100 * 2 * 2;
      if (n < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0)
        return CD_RATE;
      for (size_t pos = 12; pos + 8 <= n;) {
        uint32_t size = le32(b + pos + 4);
        if (memcmp(b + pos, "fmt ", 4) == 0)
          return pos + 20 <= n ? le32(b + pos + 16) : CD_RATE;  // nAvgBytesPerSec
        pos += 8 + size + (size & 1);
      }
      return CD_RATE;
    }
#endif
#ifdef USE_AUDIO_FLAC_SUPPORT
    case audio::AudioFileType::FLAC: {
      if (n < 26 || memcmp(b, "fLaC", 4) != 0 || (b[4] & 0x7F) != 0)  // first block must be STREAMINFO
        return 0;
      const uint8_t *si = b + 8;
      uint32_t rate = uint32_t(si[10]) << 12 | si[11] << 4 | si[12] >> 4;
      uint32_t channels = ((si[12] >> 1) & 7) + 1;
      uint32_t bits = (((si[12] & 1) << 4) | (si[13] >> 4)) + 1;
      return uint32_t(uint64_t(rate) * channels * bits / 8 * 65 / 100);
    }
#endif
#ifdef USE_AUDIO_MP3_SUPPORT
    case audio::AudioFileType::MP3:
      return 320000 / 8;
#endif
    default:
      return 0;
  }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "Content-Type") == 0)
    static_cast<PlaySession *>(evt->user_data)->content_type = evt->header_value;
  return ESP_OK;
}

// Connect, follow redirects, and work out the file type. The client is left open for the fetch task.
static const char *open_http(PlaySession *ps, audio::AudioFileType &type) {
  esp_http_client_config_t cfg = {};
  cfg.url = ps->url.c_str();
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.disable_auto_redirect = false;
  cfg.max_redirection_count = MAX_REDIRECTS;
  cfg.buffer_size = 2048;  // internal RAM; same as the audio component's reader
  cfg.keep_alive_enable = true;
  cfg.event_handler = http_event_handler;
  cfg.user_data = ps;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  if (ps->url.starts_with("https:"))
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  ps->http = esp_http_client_init(&cfg);
  if (ps->http == nullptr)
    return "out of memory";
  for (int redirects = 0;; redirects++) {
    ps->content_type.clear();
    if (esp_http_client_open(ps->http, 0) != ESP_OK)
      return "connection failed";
    ps->content_length = esp_http_client_fetch_headers(ps->http);
    int status = esp_http_client_get_status_code(ps->http);
    if (status >= 300 && status < 400 && redirects < MAX_REDIRECTS &&
        esp_http_client_set_redirection(ps->http) == ESP_OK) {
      esp_http_client_close(ps->http);
      continue;
    }
    if (status < 200 || status >= 300)
      return "HTTP error";
    break;
  }
  if (ps->content_length <= 0)
    ps->content_length = -1;
  // Servers vary in how they name WAV; detect_audio_file_type() only knows audio/wav.
  const char *ct = ps->content_type.c_str();
  char url[512];
  if (esp_http_client_get_url(ps->http, url, sizeof(url)) != ESP_OK)
    url[0] = 0;
#ifdef USE_AUDIO_WAV_SUPPORT
  if (strcasecmp(ct, "audio/x-wav") == 0 || strcasecmp(ct, "audio/wave") == 0 || strcasecmp(ct, "audio/vnd.wave") == 0)
    type = audio::AudioFileType::WAV;
  else
#endif
    type = audio::detect_audio_file_type(ct, url);
  if (type == audio::AudioFileType::NONE)
    type = audio::detect_audio_file_type(nullptr, url);
  return type == audio::AudioFileType::NONE ? "unknown or unsupported file type" : nullptr;
}

// Park a finished StaticTask; its owner deletes it with destroy().
static void park() {
  while (true)
    vTaskSuspend(nullptr);
}

void SdCacheMediaSource::play_task_(void *arg) {
  auto *self = static_cast<SdCacheMediaSource *>(arg);
  self->play_();
  self->play_running_ = false;
  self->enable_loop_soon_any_context();
  park();
}

void SdCacheMediaSource::fetch_task_(void *arg) {
  auto *ps = static_cast<PlaySession *>(arg);
  auto *buf = static_cast<uint8_t *>(heap_caps_malloc(FETCH_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  bool complete = false;
  uint32_t start = millis(), last_data = 0;
  // The first bytes of a download, for estimate_byte_rate(): a WAV's fmt chunk can sit behind JUNK/bext chunks
  // (~730 bytes into the Hatch sleep sounds), past what the first read returns.
  auto *head = ps->src == nullptr ? static_cast<uint8_t *>(heap_caps_malloc(HEADER_BYTES, MALLOC_CAP_SPIRAM))
                                   : nullptr;
  size_t head_len = 0;
  bool rated = head == nullptr;
  while (buf != nullptr && !ps->abort.load()) {
    size_t n = 0;
    if (ps->src != nullptr) {
      n = fread(buf, 1, FETCH_CHUNK, ps->src);
      if (n == 0) {
        complete = !ferror(ps->src);
        break;
      }
    } else {
      uint32_t t0 = millis();
      int r = esp_http_client_read(ps->http, reinterpret_cast<char *>(buf), FETCH_CHUNK);
      ps->max_read_ms = std::max(ps->max_read_ms, millis() - t0);
      if (r == -ESP_ERR_HTTP_EAGAIN) {
        // A read timeout: the link stalls for seconds at a time here. Keep waiting, up to STALL_GIVE_UP_MS.
        if (last_data == 0)
          last_data = t0;
        if (millis() - last_data < STALL_GIVE_UP_MS)
          continue;
      }
      if (r < 0) {
        ESP_LOGW(TAG, "Download error (%d): %s", r, ps->url.c_str());
        break;
      }
      last_data = millis();
      if (r == 0) {
        complete = esp_http_client_is_complete_data_received(ps->http) ||
                   (ps->content_length > 0 && int64_t(ps->fetched.load()) >= ps->content_length);
        break;
      }
      n = r;
      if (ps->fetched.load() == 0)
        ps->first_byte_ms = std::max<uint32_t>(1, millis());
      if (!rated) {
        size_t take = std::min(n, HEADER_BYTES - head_len);
        memcpy(head + head_len, buf, take);
        head_len += take;
        if (head_len == HEADER_BYTES) {
          ps->byte_rate = estimate_byte_rate(ps->type, head, head_len);
          rated = true;
          ESP_LOGD(TAG, "Playback needs about %" PRIu32 " KB/s", ps->byte_rate.load() / 1024);
        }
      }
    }
    ps->fetched += n;
    // Copy for the card first, all or nothing; a full buffer means the card can't keep up.
    if (ps->cache_ring != nullptr && !ps->cache_abandoned.load() &&
        ps->cache_ring->write_without_replacement(buf, n, 0, false) != n) {
      ps->abandon_reason = "card writes fell behind";
      ps->cache_abandoned = true;
    }
    size_t off = 0;
    while (off < n && !ps->abort.load())
      off += ps->input->write_without_replacement(buf + off, n - off, pdMS_TO_TICKS(50));
  }
  heap_caps_free(buf);
  heap_caps_free(head);
  if (ps->src == nullptr && ps->fetched.load() > 0) {
    uint32_t ms = std::max<uint32_t>(1, millis() - start);
    ESP_LOGD(TAG, "Downloaded %" PRIu32 " KB in %" PRIu32 " ms (%" PRIu32 " KB/s; longest read %" PRIu32 " ms)",
             uint32_t(ps->fetched.load() / 1024), ms, uint32_t(ps->fetched.load() / ms), ps->max_read_ms);
  }
  ps->fetch_complete = complete && !ps->abort.load();
  ps->fetch_done = true;
  ps->fetch_running = false;
  park();
}

void SdCacheMediaSource::write_task_(void *arg) {
  auto *self = static_cast<SdCacheMediaSource *>(arg);
  PlaySession *ps = self->session_;
  auto *buf = static_cast<uint8_t *>(heap_caps_malloc(WRITE_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    ps->abandon_reason = "out of memory";
    ps->cache_abandoned = true;
  }
  while (buf != nullptr && !ps->abort.load()) {  // on a stop the file is deleted anyway; don't drain
    size_t n = ps->cache_ring->read(buf, WRITE_CHUNK, pdMS_TO_TICKS(50));
    if (n > 0 && !ps->cache_abandoned.load()) {
      uint32_t t0 = millis();
      size_t w = fwrite(buf, 1, n, ps->cache_file);
      ps->max_write_ms = std::max(ps->max_write_ms, millis() - t0);
      ps->written += w;
      self->free_bytes_ -= w;
      if (w != n) {
        ps->abandon_reason = "card write failed";
        ps->cache_abandoned = true;
      } else if (ps->written > self->max_file_) {
        ps->abandon_reason = "bigger than max_file_mb";
        ps->cache_abandoned = true;
      } else if (self->free_bytes_.load() < self->min_free_) {
        ps->abandon_reason = "card nearly full";
        ps->cache_abandoned = true;
      }
    }
    if (n == 0 && ps->fetch_done.load())
      break;
  }
  heap_caps_free(buf);

  fclose(ps->cache_file);
  ps->cache_file = nullptr;
  bool keep = ps->fetch_complete.load() && !ps->cache_abandoned.load() && !ps->abort.load();
  if (keep && rename(ps->part_path.c_str(), ps->final_path.c_str()) == 0) {
    self->cache_bytes_ += ps->written;
    self->cache_files_++;
    ESP_LOGI(TAG, "Cached %s (%" PRIu32 " KB; longest card write %" PRIu32 " ms)",
             ps->final_path.c_str() + self->dir_path_.size() + 1, uint32_t(ps->written / 1024), ps->max_write_ms);
    self->make_room_(0, ps->final_path);
  } else {
    unlink(ps->part_path.c_str());
    self->free_bytes_ += ps->written;
    const char *why = ps->cache_abandoned.load() ? ps->abandon_reason
                      : ps->abort.load()        ? "playback stopped before the download finished"
                      : !ps->fetch_complete.load() ? "download incomplete"
                                                   : "rename failed";
    ESP_LOGW(TAG, "Not caching %s: %s", ps->url.c_str(), why);
  }
  ps->write_running = false;
  park();
}

// With repeat one, start the file again as soon as a pass ends. The source stays PLAYING throughout, so the player
// never finishes the speaker, and the decoded audio still queued in the speaker covers the next pass's start-up.
// After a miss, the writer has usually finished by the end of the pass, so later passes play from the card.
void SdCacheMediaSource::play_() {
  bool again = false;
  while (this->play_once_(again) && this->repeat_.load() && !this->stop_requested_.load())
    again = true;
}

bool SdCacheMediaSource::play_once_(bool again) {
  PlaySession session;
  PlaySession *ps = &session;
  ps->url = this->play_url_;
  if (!again)
    ESP_LOGD(TAG, "Internal RAM free %u KB (largest %u KB), DMA-capable free %u KB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);
  const bool local = ps->url.starts_with(LOCAL_SCHEME);
  const std::string key = local ? "" : this->cache_key_(ps->url);
  const bool cacheable = !local && this->play_caching_ && !this->excluded_(ps->url);

  // Cache lookup: <key>.<ext> for any supported type, or the named file for sdcache://.
  std::string cached_path;
  audio::AudioFileType type = audio::AudioFileType::NONE;
  if (local) {
    const std::string name = ps->url.substr(strlen(LOCAL_SCHEME));
    type = type_for(name.c_str());
    if (name.find('/') != std::string::npos || type == audio::AudioFileType::NONE ||
        (ps->src = fopen((this->dir_path_ + "/" + name).c_str(), "rb")) == nullptr) {
      ESP_LOGE(TAG, "Cannot open cached file %s", name.c_str());
      this->play_failed_ = true;
      return false;
    }
    if (!again)
      ESP_LOGI(TAG, "Playing cached file %s", name.c_str());
  } else if (cacheable) {
    for (const auto &ft : FILE_TYPES) {
      std::string p = this->dir_path_ + "/" + key + "." + ft.ext;
      struct stat st;
      if (stat(p.c_str(), &st) == 0) {
        cached_path = p;
        type = ft.type;
        break;
      }
    }
  }
  if (!cached_path.empty()) {
    ps->src = fopen(cached_path.c_str(), "rb");
    if (ps->src == nullptr) {
      ESP_LOGW(TAG, "Cached file %s won't open; streaming instead", cached_path.c_str());
    } else if (again) {
      ESP_LOGV(TAG, "Repeat: %s", ps->url.c_str());
    } else {
      utime(cached_path.c_str(), nullptr);  // most recently played: evicted last
      this->hits_++;
      ESP_LOGI(TAG, "Hit: %s", ps->url.c_str());
    }
  }
  this->play_hit_ = ps->src != nullptr;

  if (ps->src == nullptr) {
    // Connect here (this task has the stack for the TLS handshake); the fetch task then only reads.
    if (const char *why = open_http(ps, type)) {
      ESP_LOGE(TAG, "Cannot stream %s: %s", ps->url.c_str(), why);
      if (ps->http != nullptr)
        esp_http_client_cleanup(ps->http);
      this->play_failed_ = true;
      return false;
    }
    if (cacheable && !again)
      this->misses_++;
    ESP_LOGD(TAG, "Web: %s, Content-Type '%s', %" PRId64 " bytes", ext_for(type) ? ext_for(type) : "?",
             ps->content_type.c_str(), ps->content_length);
  }
  ps->type = type;

  // A miss may need to buffer much of the file before playing (see below), so size the buffer to the file.
  ps->input_size = DECODER_INPUT_BUFFER;
  if (ps->src == nullptr && ps->content_length > 0)
    ps->input_size = std::clamp<size_t>(size_t(std::min<int64_t>(ps->content_length, MAX_INPUT_BUFFER)),
                                        DECODER_INPUT_BUFFER, MAX_INPUT_BUFFER);
  ps->input = ring_buffer::RingBuffer::create(ps->input_size);
  if (ps->input == nullptr && ps->input_size > DECODER_INPUT_BUFFER) {
    ps->input_size = DECODER_INPUT_BUFFER;
    ps->input = ring_buffer::RingBuffer::create(ps->input_size);
  }
  if (ps->input == nullptr) {
    ESP_LOGE(TAG, "Out of memory");
    if (ps->src != nullptr)
      fclose(ps->src);
    if (ps->http != nullptr)
      esp_http_client_cleanup(ps->http);
    this->play_failed_ = true;
    return false;
  }

  if (ps->src == nullptr) {
    const char *ext = ext_for(type);
    if (cacheable && ext != nullptr) {
      ps->final_path = this->dir_path_ + "/" + key + "." + ext;
      ps->part_path = this->dir_path_ + "/" + key + PART_SUFFIX;
      if (!this->make_room_(0, "")) {
        ESP_LOGW(TAG, "No room on the card; playing without caching");
      } else if ((ps->cache_ring = ring_buffer::RingBuffer::create(CACHE_WRITE_BUFFER)) == nullptr) {
        ESP_LOGW(TAG, "No memory for the card write buffer; playing without caching");
      } else if ((ps->cache_file = fopen(ps->part_path.c_str(), "wb")) == nullptr) {
        ESP_LOGW(TAG, "Cannot create %s; playing without caching", ps->part_path.c_str());
        ps->cache_ring.reset();
      }
      ESP_LOGI(TAG, "Miss: %s%s", ps->url.c_str(), ps->cache_file ? " (caching)" : "");
    } else {
      ESP_LOGI(TAG, "Streaming%s: %s", this->play_caching_ ? " without caching" : "", ps->url.c_str());
    }
  }

  // Start the helpers. The writer reads the session through session_ (one playback at a time).
  this->session_ = ps;
  if (ps->cache_file != nullptr) {
    ps->write_running = true;
    if (!this->write_task_mem_.create(SdCacheMediaSource::write_task_, "sd_write", WRITE_TASK_STACK, this,
                                      HELPER_TASK_PRIORITY, true)) {
      ps->write_running = false;
      fclose(ps->cache_file);
      ps->cache_file = nullptr;
      unlink(ps->part_path.c_str());
      ps->cache_ring.reset();
      ESP_LOGW(TAG, "Could not start the card writer; playing without caching");
    }
  }
  ps->fetch_running = true;
  if (!this->fetch_task_mem_.create(SdCacheMediaSource::fetch_task_, "sd_fetch", FETCH_TASK_STACK, ps,
                                    HELPER_TASK_PRIORITY, true)) {
    ESP_LOGE(TAG, "Could not start the fetch task");
    ps->fetch_running = false;
    ps->fetch_done = true;
    ps->abort = true;
  }

  // Pre-buffer before decoding. A hit (or a fast link) only needs a little, against jitter. A miss over a link
  // slower than the audio's data rate needs enough that the rest of the file arrives before it is played: with R
  // bytes still to play, playback rate C and download rate D, that is R * (C - D) / C buffered (plus a margin).
  // D is measured, and planned at 85%: downloads here start with a fast burst and then slow down.
  const bool adaptive = ps->src == nullptr && ps->content_length > 0;
  auto download_rate = [ps]() -> uint64_t {
    uint32_t first = ps->first_byte_ms.load();
    return first == 0 ? 0 : ps->fetched.load() * 1000 / std::max<uint32_t>(1, millis() - first);
  };
  auto needed_buffer = [ps](uint64_t remaining, uint64_t d) -> size_t {
    uint64_t c = ps->byte_rate.load();
    d = d * 85 / 100;
    size_t need = PREBUFFER;
    if (c > d)
      need = std::max<size_t>(need, size_t(remaining * (c - d) / c * 115 / 100));
    return std::min(need, ps->input_size - 16 * 1024);
  };
  uint32_t wait_start = millis();
  bool announced = false;
  while (!ps->abort.load() && !ps->fetch_done.load() && !this->stop_requested_.load()) {
    size_t buffered = ps->input->available();
    if (!adaptive) {
      if (buffered >= PREBUFFER || millis() - wait_start >= PREBUFFER_MAX_MS)
        break;
    } else if (uint32_t first = ps->first_byte_ms.load(); first != 0 && millis() - first >= RATE_SAMPLE_MS) {
      uint64_t d = download_rate();
      size_t need = needed_buffer(uint64_t(ps->content_length), d);
      if (!announced && need > PREBUFFER) {
        announced = true;
        ESP_LOGI(TAG, "Download %" PRIu32 " KB/s is slower than playback (%" PRIu32 " KB/s); buffering %" PRIu32
                      " KB first so it doesn't stutter",
                 uint32_t(d / 1024), ps->byte_rate.load() / 1024, uint32_t(need / 1024));
      }
      if (buffered >= need)
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (announced)
    ESP_LOGI(TAG, "Buffered %" PRIu32 " KB in %" PRIu32 " ms; playing", uint32_t(ps->input->available() / 1024),
             millis() - wait_start);

  bool failed = ps->abort.load();
  if (!failed) {
    audio::AudioDecoder decoder(CHUNK, 8192);
    OutputSink sink(this, &decoder, &this->paused_, &this->stop_requested_);
    std::weak_ptr<ring_buffer::RingBuffer> weak_input = ps->input;
    if (decoder.add_source(weak_input) != ESP_OK || decoder.add_sink(&sink) != ESP_OK ||
        decoder.start(type) != ESP_OK) {
      ESP_LOGE(TAG, "Decoder setup failed");
      failed = true;
    }
    uint32_t max_gap = 0, last = millis(), rebuffers = 0;
    while (!failed && !this->stop_requested_.load()) {
      // The download fell behind anyway: pause once, long enough for the rest of the file, instead of stuttering.
      if (adaptive && !ps->fetch_done.load() && ps->input->available() < REBUFFER_LOW_WATER) {
        uint64_t played = ps->fetched.load() - ps->input->available();
        uint64_t remaining = uint64_t(ps->content_length) > played ? uint64_t(ps->content_length) - played : 0;
        uint64_t d = download_rate();
        size_t need = needed_buffer(remaining, d);
        ESP_LOGW(TAG, "Download fell behind (%" PRIu32 " KB/s); pausing to buffer %" PRIu32 " KB", uint32_t(d / 1024),
                 uint32_t(need / 1024));
        uint32_t t0 = millis();
        while (!ps->fetch_done.load() && !this->stop_requested_.load() && ps->input->available() < need)
          vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Resumed after %" PRIu32 " ms", millis() - t0);
        rebuffers++;
        last = millis();
      }
      max_gap = std::max(max_gap, millis() - last);
      audio::AudioDecoderState ds = decoder.decode(ps->fetch_done.load());
      last = millis();

      if (ds == audio::AudioDecoderState::FINISHED)
        break;
      if (ds == audio::AudioDecoderState::FAILED) {
        ESP_LOGE(TAG, "Decode failed: %s", ps->url.c_str());
        failed = true;
      }
      if (this->paused_.load())
        vTaskDelay(pdMS_TO_TICKS(PAUSE_POLL_MS));
    }
    ESP_LOGD(TAG, "Longest gap between decodes %" PRIu32 " ms, longest network read %" PRIu32 " ms, rebuffers %" PRIu32,
             max_gap, ps->max_read_ms, rebuffers);
  }

  // Wind down: a stop or failure aborts the helpers; a normal end lets the writer finish the file.
  if (failed || this->stop_requested_.load())
    ps->abort = true;
  while (ps->fetch_running.load() || ps->write_running.load())
    vTaskDelay(pdMS_TO_TICKS(10));
  while (!this->fetch_task_mem_.destroy() || !this->write_task_mem_.destroy())
    vTaskDelay(pdMS_TO_TICKS(1));
  this->session_ = nullptr;
  if (ps->src != nullptr)
    fclose(ps->src);
  if (ps->http != nullptr) {
    esp_http_client_close(ps->http);
    esp_http_client_cleanup(ps->http);
  }
  if (ps->cache_file != nullptr) {  // writer never started
    fclose(ps->cache_file);
    unlink(ps->part_path.c_str());
  }
  this->play_failed_ = failed && !this->stop_requested_.load();
  return !failed && !this->stop_requested_.load();
}

}  // namespace esphome::sd_cache

#endif  // USE_ESP32
