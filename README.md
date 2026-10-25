# hatch-riot-re

Rest+ development: [build privately and run the local WSUP flasher](flasher/README.md).
Application-only images; no public credentials or stock binaries are distributed.
See the [Rest+ 2 ESPHome sample guide](docs/rest-plus-2-esphome.md) for display,
button gestures, and automatic stock-sound looping. Power safety remains unvalidated.


Reverse-engineering notes for the Hatch **"riot"** device firmware, and a working [ESPHome](https://esphome.io)
replacement firmware for it.

## Status: ESPHome replacement working ✅

**[`esphome/hatch-rest.yaml`](esphome/hatch-rest.yaml) runs on a Hatch Rest (2nd gen) and every feature was tested
on the device** (2026-10-12):

| Feature | Result |
|---|---|
| RGBW night light (colour, white, brightness) | ✅ |
| Speaker — **Music Assistant via Sendspin** (auto-discovered), plus Home Assistant media player | ✅ (no start-up pop) |
| Front buttons (volume ±, brightness ±, night light), touch top, bottom button | ✅ |
| Toddler lock (disables the buttons, lock icon lit) | ✅ |
| Clock display (12 h, brightness) | ✅ |
| Wi-Fi / lock status LEDs | ✅ |
| OTA updates | ✅ (with bootloader rollback) |

Known gaps: Home Assistant TTS / URL playback depends on HA's ffmpeg proxy (it returned empty streams in testing;
Music Assistant is unaffected), the status LEDs show on a short press of the bottom button (for 30 s) rather than
when tipped over like stock, the bottom button has no hold action, and **RIoT+ (Rest+ 2nd gen) is not supported**. Details and the component list are in
[docs/esphome-plan.md](docs/esphome-plan.md#replacement-config); flashing and setup are in
[esphome/README.md](esphome/README.md). Back up the flash with [`scripts/backup.sh`](scripts/backup.sh) first.

The hardware map was worked out by **static analysis** of the stock firmware (Capstone, then Ghidra) and confirmed
pin by pin on the device with a diagnostic [probe firmware](docs/probe-firmware.md) and a
[teardown](docs/teardown.md). See [docs/open-questions.md](docs/open-questions.md) for what's still open.

## Target

| | |
|---|---|
| Device | Hatch **Rest (2nd gen)**, model **HBREST2** (confirmed by [teardown](docs/teardown.md)) |
| File | `riot_e831cd79dc02_riot_DFU_7_1_644.bin` (not committed; see [Getting the image](#getting-the-image)) |
| Format | ESP-IDF application image, chip ESP32 (`chip_id 0`), SHA-256 appended |
| Project / version | `riot`, `X.711.644-dirty` (DFU 7.1.644), built Sep 25 2026 |
| SDK | ESP-IDF v5.5.4 |
| Module | **ESP32-WROVER-E-N16R8** (PCB antenna, 16 MB flash, 8 MB PSRAM); built with `-mfix-esp32-psram-cache-issue` |
| Variants | firmware detects **"RIoT"** (Rest 2nd gen, this unit) and **"RIoT+"** (Rest+ 2nd gen) at boot |

## Verdict

An ESPHome port is **feasible — and now done** (see [Status](#status-esphome-replacement-working-)). The SoC is a stock ESP32 (D0WD-V3), and its eFuses read **unlocked** on the
checked unit — no flash encryption, no secure boot — so it can be backed up and reflashed over serial. Most
peripherals map onto ESPHome core components:

- **Night light:** 4 PWM channels on `ledc`, used as an RGBW light.
- **Sound:** I2S to a digital amp with no codec, on `i2s_audio` and `speaker`.
- **I2C buses** and direct GPIO inputs.

These parts needed custom code (local components in [`esphome/components/`](esphome/components)):

- **Clock display:** a separate display MCU fed over a TX-only UART with a simple framed protocol. See
  [docs/display-protocol.md](docs/display-protocol.md).
- **GPIO expander:** Awinic at 0x58, driven by the stock firmware's AW9110B driver (chip ID 0x23, possibly an
  AW9523B). Enables on P1_0–P1_3, buttons on P0_0–P0_4, touch enable on P0_5. Its I2C bus is unreliable with
  ESPHome's I2C driver, so `hatch_expander` uses its own bit-banged I2C.
- **Status LEDs:** two red/green bicolour LEDs anti-parallel on GPIO23 / GPIO13 / GPIO32, colour chosen by PWM phase.
- **Accelerometer** (DA213): orientation only on this unit (stock uses it to show the status LEDs when tipped over).
- **RIoT+ power path:** a GreenPAK (power latch, battery comparator) and a wireless-power IC. This is the riskiest
  part.

## Docs

- [docs/hardware-map.md](docs/hardware-map.md): pins, buses, I2C addresses and peripheral config, each with the
  code address it came from.
- [docs/teardown.md](docs/teardown.md): step-by-step disassembly with photos — confirms the device (Hatch Rest 2nd
  gen, HBREST2) and the module (ESP32-WROVER-E, PCB antenna).
- [docs/serial-flashing.md](docs/serial-flashing.md): soldering to the ESP32 module, backing up the stock flash and
  the first ESPHome flash over serial.
- [docs/display-protocol.md](docs/display-protocol.md): clock display UART protocol, font and brightness tables.
- [docs/firmware-architecture.md](docs/firmware-architecture.md): firmware tasks, components and cloud stack.
- [docs/esphome-plan.md](docs/esphome-plan.md): **the working replacement config**, component mapping, and a safe
  flashing procedure.
- [docs/open-questions.md](docs/open-questions.md): unresolved items and how to resolve them on hardware.
- [docs/methodology.md](docs/methodology.md): how the analysis was done, and how to reproduce it with `tools/`.
- [docs/probe-firmware.md](docs/probe-firmware.md): **safety model and test plan for the ESPHome probe firmware**.

## ESPHome firmware

- [`esphome/hatch-rest.yaml`](esphome/hatch-rest.yaml): **the stock replacement** (builds with ESPHome 2026.9.1),
  with local components `hatch_expander`, `hatch_display` and `hatch_status_led`.
- `esphome/hatch-riot-probe.yaml`: a read-mostly diagnostic firmware used to confirm the hardware map, plus a local
  `riot_probe` component.
- `scripts/backup.sh`: read-only eFuse check plus a full-flash backup, read twice and verified. **Run this before
  flashing anything.**
- `scripts/restore.sh`: write a backup back to the device (return to stock).

### Hatch Rest+ 2 local scenes

The [Rest+ 2 example](esphome/hatch-plus-example.yaml) stores ten device-local
presets, including light, stock sound/volume, clock brightness and text. They
work without Home Assistant. `store_preset(index, name)` and `apply_preset(index)`
are available through the native API.

Button choices live in `hatch_plus.buttons`: use `tap: next` on the top touch
button to cycle, or `tap: none` to disable it. Set `hold: manage_preset` on your
chosen save/delete button. Release at 5–<10 seconds to save a new slot (`SAVn`);
hold to 10 seconds to preview `DELn`, then release to delete. See the
[scene configuration guide](docs/rest-plus-2-esphome.md#on-device-scene-buttons).

### Hatch Rest+ 2 button blueprint

[`blueprints/automation/hatch-rest-plus-2/buttons.yaml`](blueprints/automation/hatch-rest-plus-2/buttons.yaml)
provides short, long, double and triple press actions for all seven Rest+ 2
buttons using ESPHome firmware events. Set the event device name and any custom
actions; no raw touch sensor or favorites configuration is required. Native
firmware actions remain active.

[![Import Rest+ 2 blueprint](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2Fmartinemde%2Fhatch-riot-re%2Fblob%2Fmain%2Fblueprints%2Fautomation%2Fhatch-rest-plus-2%2Fbuttons.yaml)

### Touch ring blueprint

[`blueprints/automation/hatch-rest/touch-ring-favorites.yaml`](blueprints/automation/hatch-rest/touch-ring-favorites.yaml)
brings back the stock touch ring in Home Assistant: **tap** to cycle through a list of favorites (scenes or scripts
that set the light and speaker), **touch and hold** (1 s, like stock) to turn the light and sound off. Pick the touch
sensor, light, media player and favorites when creating the automation; optionally pick the Toddler Lock switch so
the ring is ignored while it's on. Needs Home Assistant 2024.10 or later.

[![Import blueprint](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2Fw1ll1am23%2Fhatch-riot-re%2Fblob%2Fmain%2Fblueprints%2Fautomation%2Fhatch-rest%2Ftouch-ring-favorites.yaml)

Each tap activates the favorite after the one used most recently. While the light and speaker are both off, a tap
starts from the first favorite instead (configurable: resume the last one, or go to the next). A scene can set the
light plus the speaker's volume and media; use a script for anything a scene can't do.

## Getting the image

Images are downloaded with the fetcher in [`fetcher/`](fetcher/README.md), using your own Hatch account; it saves
them to `firmware/`, where the tools expect them. Firmware binaries are vendor property and are git-ignored here.

## Reproducing

```sh
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
mkdir -p work
.venv/bin/python -I tools/split_segments.py firmware/riot_*.bin work
.venv/bin/python -I tools/dis.py firmware/riot_*.bin work/listing.txt
.venv/bin/python -I tools/xr.py work/listing.txt
.venv/bin/python -I tools/q.py work/listing.txt.xr byname hal_check_riot_hardware
.venv/bin/python -I tools/nm.py work/listing.txt.xr 40139334 | .venv/bin/python -I tools/dd.py
```

See [docs/methodology.md](docs/methodology.md) for details.
