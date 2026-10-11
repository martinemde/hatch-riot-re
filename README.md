# hatch-riot-re

Reverse-engineering notes for the Hatch **"riot"** device firmware. The aim is to judge, and eventually build, an
[ESPHome](https://esphome.io) replacement firmware that controls the same hardware.

Findings come from **static analysis** of one firmware image, with the hardware map now partly confirmed by a
[teardown](docs/teardown.md). Behaviour has not been checked on a running device. See
[docs/open-questions.md](docs/open-questions.md) for what still needs hardware confirmation.

## Target

| | |
|---|---|
| Device | Hatch **Rest (2nd gen)**, model **HBREST2** (confirmed by [teardown](docs/teardown.md)) |
| File | `riot_e831cd79dc02_riot_DFU_7_1_644.bin` (not committed; see [Getting the image](#getting-the-image)) |
| Format | ESP-IDF application image, chip ESP32 (`chip_id 0`), SHA-256 appended |
| Project / version | `riot`, `X.711.644-dirty` (DFU 7.1.644), built Sep 25 2026 |
| SDK | ESP-IDF v5.5.4 |
| Module | **ESP32-WROVER-E** (PCB antenna, 8 MB PSRAM); built with `-mfix-esp32-psram-cache-issue` |
| Variants | firmware detects **"RIoT"** (Rest 2nd gen, this unit) and **"RIoT+"** (Rest+ 2nd gen) at boot |

## Verdict

An ESPHome port is **feasible**. The SoC is a stock ESP32. I found no Secure Boot v2 signature block appended to the
app image. Most peripherals map onto ESPHome core components:

- **Night light:** 4 PWM channels on `ledc`, used as an RGBW light.
- **Sound:** I2S to a digital amp with no codec, on `i2s_audio` and `speaker`.
- **I2C buses** and direct GPIO inputs.

These parts need custom code:

- **Clock display:** a separate display MCU fed over a TX-only UART with a simple framed protocol. See
  [docs/display-protocol.md](docs/display-protocol.md).
- **GPIO expander:** AW9523B (alternatively AW9110B) that carries enable lines and buttons.
- **Accelerometer** used for tap detection.
- **RIoT+ power path:** a GreenPAK (power latch, battery comparator) and a wireless-power IC. This is the riskiest
  part.

## Docs

- [docs/hardware-map.md](docs/hardware-map.md): pins, buses, I2C addresses and peripheral config, each with the
  code address it came from.
- [docs/teardown.md](docs/teardown.md): annotated disassembly photos — confirms the device (Hatch Rest 2nd gen,
  HBREST2) and the module (ESP32-WROVER-E, PCB antenna).
- [docs/display-protocol.md](docs/display-protocol.md): clock display UART protocol, font and brightness tables.
- [docs/firmware-architecture.md](docs/firmware-architecture.md): firmware tasks, components and cloud stack.
- [docs/esphome-plan.md](docs/esphome-plan.md): component mapping, draft YAML, and a safe flashing procedure.
- [docs/open-questions.md](docs/open-questions.md): unresolved items and how to resolve them on hardware.
- [docs/methodology.md](docs/methodology.md): how the analysis was done, and how to reproduce it with `tools/`.
- [docs/probe-firmware.md](docs/probe-firmware.md): **safety model and test plan for the ESPHome probe firmware**.

## Confirming on hardware

- `esphome/`: `hatch-riot-probe.yaml`, a read-mostly diagnostic ESPHome firmware (builds with ESPHome 2026.9.1),
  plus a local `riot_probe` component.
- `scripts/backup.sh`: read-only eFuse check plus a full-flash backup, read twice and verified. **Run this before
  flashing anything.**
- `scripts/restore.sh`: write a backup back to the device (return to stock).

## Getting the image

Images are downloaded with the fetcher in the sibling repo `~/Dev/hatch_firmware` (`hatch_firmware.py`). Firmware
binaries are vendor property and are git-ignored here. Put the image in `firmware/` to use the tools.

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
