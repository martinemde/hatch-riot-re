# Stock firmware architecture

This is a high-level view, useful for knowing which behaviours an ESPHome port would need to rebuild. The source
paths come from embedded `__FILE__` strings (`../../src/esp/primary/components/...`).

## Components

| Component | Purpose |
|---|---|
| `tasks/LED` | LED task: LEDC setup, colour series, fades (`LED_task_main`, `LED_fade_task`) |
| `tasks/audio_task`, `audio_driver/` | Playback (ESP-ADF style decoders for WAV/MP3), I2S output, amp control, volume and fades |
| `tasks/Touch_Buttons/Button_handler.c`, `tasks/Butts_Segs` | Button and tap handling, segment-display updates |
| `seg_display/` | Display event stack and scheduler (`seg_scheduler_*`) |
| `tasks/Display_task` | Display task |
| `greenpak/` | GreenPAK find, read, write, NVM program, virtual inputs |
| `brownout/` | Custom brownout handling |
| `sd/` | SD mount, file cache, health checks, logging to SD |
| `web_streamer/` | Content scheduler and downloader (fetches sounds and colour files to SD) |
| `tasks/WiFi_task` | Wi-Fi STA, enterprise EAP with certificate pinning, credential list |
| `tasks/Bluetooth` | BLE GATT server for setup/provisioning (Bluedroid) |
| `tasks/IoT_task` | AWS IoT over coreMQTT: shadow-like state, routine/RDL parser, ALS data publish |
| `tasks/OTA_task`, `helpers/OTA_Helpers` | OTA from `firmware.hatchbaby.com`, rollback "three-pings" validation |
| `tasks/SRoutine_task`, `helpers/Routine_helpers` | Routines: light + sound steps, favourites, button controls |
| `tasks/Command` | Serial/BLE command interface (e.g. `TLRdd`, `Sc%2hx%2hx%2hx%2hx` raw colour, factory tests) |
| `icmp_ping/` | Connectivity checks |

## Routines (content model)

Default routines are embedded as JSON. A step pairs a **colour** with a **sound**:

```json
{"id":2,"steps":[{"color":{"id":2,"i":32767,"until":"indefinite",
  "series":[{"r":65535,"g":0,"b":0,"w":0,"duration":null}]},
  "sound":{"id":10146,"v":32767,
  "url":"https://images.hatchbaby.com/content/sounds/Thunderstorm_20210412.wav",
  "until":"indefinite"}}]}
```

- Colour channels are **16-bit** (0–65535). Intensity `i` and volume `v` are 0–32767.
- `controls.button_0` / `button_1` list the routine IDs that each physical button cycles through.

## Factory and debug commands

The test hooks show the hardware blocks directly: `test_red_leds`, `test_green_leds`, `test_blue_leds`,
`test_white_leds`, `test_amplifier_power`, `test_sd_card`, `test_num_display`, `test_on_display`,
`test_graphics_display_animation`, `set_raw_color` (`Sc` + four 16-bit hex values), `set_segment_brightness`
(`TLRdd`).

## Cloud dependencies (not needed for ESPHome)

- AWS IoT MQTT (`$aws/rules/als/...`).
- `firmware.hatchbaby.com` (OTA), `images.hatchbaby.com` (sounds), `time.hatchbaby.com` / SNTP.
- `clients3.google.com` (connectivity check).
