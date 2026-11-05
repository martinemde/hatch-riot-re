from esphome import pins
import esphome.codegen as cg
from esphome.components import audio, media_source, psram
from esphome.components.esp32 import (
    include_builtin_idf_component,
    require_fatfs,
    require_vfs_dir,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

AUTO_LOAD = ["audio"]
DEPENDENCIES = ["psram"]

CONF_POWER_PIN = "power_pin"
CONF_DIRECTORY = "directory"
CONF_MAX_SIZE_MB = "max_size_mb"
CONF_MIN_FREE_MB = "min_free_mb"
CONF_MAX_FILE_MB = "max_file_mb"
CONF_EXCLUDE = "exclude"
CONF_IGNORE_QUERY_PARAMS = "ignore_query_params"
CONF_LIBRARY_ENABLED = "library_enabled"

sd_cache_ns = cg.esphome_ns.namespace("sd_cache")
SdCacheMediaSource = sd_cache_ns.class_(
    "SdCacheMediaSource", cg.Component, media_source.MediaSource
)


def _cache_directory(value):
    value = cv.string_strict(value)
    reserved = {
        "rest_sounds", "restore_sounds", "restmini_sounds", "sleep_sounds",
        "downloaded", "alarm_sounds", "logs",
    }
    if not value or len(value) > 32 or value.lower() in reserved:
        raise cv.Invalid("Cache directory must be a separate non-stock top-level directory")
    if any(not (c.isascii() and (c.isalnum() or c in "_-")) for c in value):
        raise cv.Invalid("Cache directory may contain only ASCII letters, numbers, '_' and '-'")
    return value


def _request_decoders(config: ConfigType) -> ConfigType:
    # The cache decodes with the audio component's AudioDecoder (not micro-decoder), so it needs these codecs.
    audio.request_mp3_support()
    audio.request_flac_support()
    audio.request_wav_support()
    return config


CONFIG_SCHEMA = cv.All(
    media_source.media_source_schema(SdCacheMediaSource)
    .extend(
        {
            # SD card power (expander P1_1 on the Hatch).
            cv.Required(CONF_POWER_PIN): pins.gpio_output_pin_schema,
            # Folder on the card that holds the cache. Nothing outside it is ever written.
            cv.Optional(CONF_DIRECTORY, default="esphome_cache"): _cache_directory,
            cv.Optional(CONF_LIBRARY_ENABLED, default=False): cv.boolean,
            # Total size the cache may grow to; least recently played files are deleted past it.
            cv.Optional(CONF_MAX_SIZE_MB, default=400): cv.int_range(min=1),
            # Always leave at least this much free on the card.
            cv.Optional(CONF_MIN_FREE_MB, default=50): cv.int_range(min=0),
            # Files bigger than this (and endless streams) play without being cached.
            cv.Optional(CONF_MAX_FILE_MB, default=100): cv.int_range(min=1),
            # URLs containing any of these are never cached. TTS is unique per message.
            cv.Optional(CONF_EXCLUDE, default=["/api/tts_proxy/"]): cv.ensure_list(
                cv.string_strict
            ),
            # Query parameters dropped before the URL is used as a cache key (Home Assistant signs media URLs).
            cv.Optional(
                CONF_IGNORE_QUERY_PARAMS, default=["authSig"]
            ): cv.ensure_list(cv.string_strict),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    _request_decoders,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await media_source.register_media_source(var, config)

    cg.add(var.set_power_pin(await cg.gpio_pin_expression(config[CONF_POWER_PIN])))
    cg.add(var.set_directory(config[CONF_DIRECTORY]))
    cg.add(var.set_library_enabled(config[CONF_LIBRARY_ENABLED]))
    cg.add(var.set_max_size_mb(config[CONF_MAX_SIZE_MB]))
    cg.add(var.set_min_free_mb(config[CONF_MIN_FREE_MB]))
    cg.add(var.set_max_file_mb(config[CONF_MAX_FILE_MB]))
    for pattern in config[CONF_EXCLUDE]:
        cg.add(var.add_exclude(pattern))
    for param in config[CONF_IGNORE_QUERY_PARAMS]:
        cg.add(var.add_ignore_query_param(param))

    # SDMMC + FAT. ESPHome excludes these by default; fatfs also pulls in wear_levelling and esp_driver_sdspi.
    for idf_component in (
        "esp_driver_sdmmc",
        "sdmmc",
        "esp_driver_sdspi",
        "wear_levelling",
        "fatfs",
    ):
        include_builtin_idf_component(idf_component)
    require_fatfs()  # long file names
    require_vfs_dir()  # opendir/readdir for eviction
    # The playback task stacks go in PSRAM, leaving internal RAM for the I2S DMA buffers.
    psram.request_external_task_stack()
