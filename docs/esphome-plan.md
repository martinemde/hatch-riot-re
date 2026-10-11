# ESPHome port plan

## Component mapping

| Hardware | ESPHome | Effort |
|---|---|---|
| ESP32-WROVER + PSRAM | `esp32` (`esp-idf` framework), `psram` | none |
| 2 × I2C | `i2c` (bus 1 only on RIoT+) | none |
| RGBW night light | 4 × `ledc` output + `light: rgbw`, `max_power` per channel | low |
| Speaker | `i2s_audio` + `speaker` + `media_player` (play URLs from Home Assistant) | low–medium |
| Expander enables and buttons | AW9523B: no core component as far as I know. Use an external component or raw I2C writes in lambdas. | medium |
| TOUCH / CONNECT inputs | `binary_sensor: gpio` | none |
| Clock display | `uart` + lambda (see [display-protocol.md](display-protocol.md)), `time` | low |
| Tap detection | custom/external component for the MiraMEMS accelerometer | medium |
| SD card | skip; stream sounds from Home Assistant or embed short clips | n/a |
| RIoT+ battery/power | custom GreenPAK + wireless-power handling | **high / risky** |

## Draft YAML (RIoT, untested)

```yaml
esphome:
  name: hatch-riot
  on_boot:
    priority: 600
    then:
      # Enable the LED driver and amp on the AW9523B (0x58).
      # Output port registers: 0x02 (P0), 0x03 (P1). Config registers: 0x04/0x05 (0 = output).
      # Bit assignments are UNCONFIRMED; see open-questions.md #1.
      - lambda: |-
          auto *bus = id(bus_a);
          uint8_t cfg0[2] = {0x04, 0xF8};        // P0_0..2 outputs, rest inputs
          bus->write(0x58, cfg0, 2);
          uint8_t out0[2] = {0x02, 0x05};        // LED_EN (bit0) + AMP_EN (bit2) high
          bus->write(0x58, out0, 2);

esp32:
  board: esp-wrover-kit
  framework:
    type: esp-idf

psram:

logger:
api:
ota:
  - platform: esphome
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

i2c:
  - id: bus_a
    sda: GPIO0
    scl: GPIO22
    frequency: 400kHz
    scan: true

output:
  - {platform: ledc, id: ch_r, pin: GPIO5,  frequency: 19500Hz, max_power: 50%}
  - {platform: ledc, id: ch_g, pin: GPIO18, frequency: 19500Hz, max_power: 37.5%}
  - {platform: ledc, id: ch_b, pin: GPIO19, frequency: 19500Hz, max_power: 25%}
  - {platform: ledc, id: ch_w, pin: GPIO4,  frequency: 19500Hz, max_power: 25%}

light:
  - platform: rgbw
    name: Night Light
    red: ch_r
    green: ch_g
    blue: ch_b
    white: ch_w

i2s_audio:
  - id: i2s_out
    i2s_lrclk_pin: GPIO25
    i2s_bclk_pin: GPIO26

speaker:
  - platform: i2s_audio
    id: spk
    i2s_audio_id: i2s_out
    i2s_dout_pin: GPIO21
    dac_type: external
    channel: right
    bits_per_sample: 16bit

media_player:
  - platform: speaker
    name: Hatch Speaker
    announcement_pipeline:
      speaker: spk

uart:
  id: disp_uart
  tx_pin: GPIO27
  baud_rate: 19200
  parity: ODD

time:
  - platform: homeassistant
    id: ha_time
    on_time:
      - seconds: 0
        then:
          - script.execute: show_time

binary_sensor:
  - platform: gpio
    name: Touch
    pin: GPIO33
  - platform: gpio
    name: Connect
    pin: {number: GPIO36, inverted: true}

# script: show_time (see display-protocol.md)
```

## Safe flashing procedure

1. **Find the serial pads.** You need UART0 (GPIO1 TX / GPIO3 RX), EN and GPIO0. GPIO0 is also I2C0 SDA here, so
   the boot strap may need care.
2. **Check eFuses** before writing anything:
   `espefuse.py --port … summary`. Look at `FLASH_CRYPT_CNT` (flash encryption) and `ABS_DONE_0/1` (secure boot).
   If either is set, stop and reassess.
3. **Back up the whole flash:** `esptool.py --port … read_flash 0 ALL hatch_full_backup.bin`. This keeps the
   NVS (device certificates, Wi-Fi, calibration) needed to return to stock.
4. Read the partition table from the backup (`gen_esp32part.py`) and record the layout.
5. Flash ESPHome over serial. Keep the backup so you can restore with `write_flash 0 hatch_full_backup.bin`.
6. **Later updates go over the air.** Only this first flash needs serial — the stock firmware will not accept a
   third-party OTA image, but once ESPHome is on, its own OTA (with bootloader rollback) handles the rest. See
   [probe-firmware.md](probe-firmware.md#updating-over-the-air).

## Replacing the module

If the installed chip is locked (flash encryption / secure boot in eFuses) or dead, the Wi-Fi module can be
reworked. Confirmed from the [teardown](teardown.md): the part is an **ESP32-WROVER-E, PCB-antenna variant**
(8 MB PSRAM), on the "RIoT Main V1.5" board.

- **Buy:** genuine Espressif **ESP32-WROVER-E** (not a clone), **PCB antenna** (not the IPEX/U.FL version).
- **Flash size:** read the installed part with `esptool … flash-id` and match it (`N4` = 4 MB, `N8` = 8 MB,
  `N16` = 16 MB; the app-image header says 16 MB → likely **N16R8**). Verify before buying.
- A fresh module has **blank eFuses**, so it clears a locked-chip problem — but it's one-way: you lose the stock
  firmware's encrypted NVS (device certs, Wi-Fi), so you can't go back to stock afterward.
- This is module-level hot-air rework (castellated 18×31 mm module), not bare-QFN work.

## Suggested order

1. Serial access + backup + eFuse check.
2. I2C scan on bus 0 (expect 0x58, 0x26, maybe 0x70; on RIoT+ also 0x10 and 0x60 on either bus).
3. Read the expander input registers while pressing each button → settle the pin map.
4. LEDs (after the LED-enable line is confirmed).
5. Audio (after amp enable).
6. Display UART.
7. Accelerometer tap.
8. RIoT+ power path, last.
