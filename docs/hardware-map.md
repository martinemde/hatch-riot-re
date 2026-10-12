# Hardware map

Each entry is tagged:

- **[O] observed:** read directly from code or constant data. The address is given.
- **[I] inferred:** a reasonable reading, not directly proven.
- **[O, hw]:** confirmed on the physical board — see [teardown.md](teardown.md).

All addresses are virtual addresses in firmware 7.1.644. Function names in `<>` are auto-names taken from strings
the function references (see [methodology.md](methodology.md)).

**Device:** Hatch **Rest (2nd gen)**, model **HBREST2** — the firmware's "RIoT". Main board silkscreen "RIoT Main
V1.5" (2021-06-5). [O, hw]

## SoC

| Item | Value | Tag / evidence |
|---|---|---|
| Chip | **ESP32-D0WD-V3** (classic, dual-core Xtensa LX6, rev v3.0, 240 MHz) | [O, hw] `esptool` chip detect; [O] image header `chip_id 0` |
| Module | **ESP32-WROVER-E-N16R8**, PCB-antenna variant | [O, hw] can marking (photo 11); PSRAM driver + himem use → 8 MB |
| Flash | 16 MB, JEDEC `20 40 18` (manufacturer `0x20` = XMC) | [O, hw] `esptool flash-id`; XMC matches the firmware's `Unsupported XMC model` handling |
| Security | **eFuses unlocked** — flash encryption OFF (`FLASH_CRYPT_CNT=0`), secure boot v1/v2 OFF (`ABS_DONE_0/1=False`), UART download + JTAG enabled, no RD/WR-disable | [O, hw] `espefuse summary` |
| SDK | ESP-IDF v5.5.4 | [O] app descriptor |
| Entry | `0x40081b5c` | [O] image header |

Image segments:

| # | Load address | Length | Region |
|---|---|---|---|
| 0 | `0x3f400020` | `0xc218c` | DROM (rodata, strings) |
| 1 | `0x3ffbdb60` | `0xde64` | DRAM (.data) |
| 2 | `0x400d0020` | `0x1571f0` | IROM (flash code) |
| 3 | `0x3ffcb9c4` | `0xa498` | DRAM |
| 4 | `0x40080000` | `0x1d754` | IRAM |
| 5 | `0x50000298` | `0x80` | RTC slow memory |

## Board-variant detection

`<hal_riot_check_i2c_pins>` at `0x401391d8` [O]:

- Configures **GPIO13, GPIO32, GPIO23** as inputs with the internal pull-down on.
- Waits 50 ticks, then reads them.
- All three low → **"RIoT"** (hw type 1). Logs `RIoT Hardware found.`
- Pins 13 and 32 high (and 23 high) → **"RIoT+"** (hw type 2). Logs `RIoT Plus hardware found.`
- Anything else → logs an error and defaults to RIoT+.

**[O, hw]** The probe firmware read `GPIO13=0 GPIO32=0 GPIO23=0 → RIoT` on the teardown unit, confirming both the
pins and the all-low = RIoT decoding.

[I] On RIoT+ these pins carry the second I2C bus, so the bus's external pull-ups pull them high. The hw type is
stored at `0x3ffd8844` and read through `0x40139d14`.

## I2C

`<i2c_task_init>` at `0x401c6190` [O]. Both buses use legacy `i2c_param_config` in master mode at
**400 kHz** (`0x61a80`), with internal pull-ups **disabled**.

| Bus | SDA | SCL | Present on |
|---|---|---|---|
| I2C0 | **GPIO0** | **GPIO22** | all |
| I2C1 | **GPIO13** | **GPIO23** | RIoT+ only (set up when hw type == 2) |

All device traffic goes through a single I2C job task (`i2c_task_main`). It sends dummy transactions to recover
the bus at boot.

**[O, hw]** Probe-firmware scan of bus 0 (GPIO0/GPIO22) on the RIoT teardown unit: devices at **0x27** and
**0x58**. 0x58 is the expander (ID read `0x23`, below). 0x27 was not predicted — see the accelerometer row and
[open-questions.md #5/#6](open-questions.md). 0x10/0x60 absent (RIoT+ only), 0x70 absent (display is UART, not I2C).

### I2C devices

The firmware stores 8-bit (R/W-shifted) addresses. The 7-bit address is listed here.

| Device | 7-bit addr | Evidence | Notes |
|---|---|---|---|
| GPIO expander, Awinic (**AW9110B driver**; chip possibly AW9523B) | **0x58** | [O] `gpioe_init_device` `0x401c7018` stores `0xB0`; check in `0x40139bb8` compares to `0xB0`; **[O, hw]** probe read ID reg `0x10` = `0x23` | **[O, Ghidra]** the stock firmware drives it with the **AW9110B** driver (`gpioe_init_device(0)` leaves type = 0 → `gpioe_aw9110b_config_pins`, 10 pins). ID `0x23` is AW9523B's ID, but the AW9110B may report the same — chip identity unconfirmed (mark on the package would settle it). Register layout 0x00–0x07/0x11 is compatible either way. Interrupt on GPIO34. Boot snapshot `OUT=0000 CFG=0000 GCR=00`. |
| GreenPAK (Renesas SLG46xxx) | **0x10** programmed, **0x08** blank | [O] `<greenpak_find>` `0x40117444` uses 8-bit `0x20` / `0x10` | Power latch, battery-voltage comparator (adjustable Vref), charge enable. The firmware compares and can **reprogram the GreenPAK NVM** (`greenpak_program_nvm`). RIoT+ only. |
| Accelerometer, MiraMEMS **MSA301 / DA213** (MSA3xx family) | firmware **0x26**; **0x27 on this unit** | [O] `<accelerometer_init>` `0x400ff590` uses 8-bit `0x4c` (→ 7-bit 0x26); soft-reset reg `0x00` ← `0x24`. **[O, hw]** probe dump of 0x27 read **reg 0x01 (part ID) = `0x13`** = MSA301/DA213 WHO_AM_I. | On bus 0 at **0x27** (address pin strapped high vs. the firmware's nominal 0x26). Used for single-tap detection ("top button tap"). See [open-questions.md #6](open-questions.md). |
| Wireless-power IC | **0x60** | [O] `<wireless_power_find>` `0x401dffe8` uses 8-bit `0xC0` | Has status, temperature and output-voltage set. [I] RIoT+ only. |
| Display MCU (I2C side) | **0x70** | [O] `<GPIO_Clock_IIC_shutdown>` `0x401380f4` writes `0x20` to 8-bit `0xE0` | Only used for shutdown. Display data goes over UART (below). |

## GPIO expander lines

`set_level(dev, pin, level)`. The level enum is 0 = INPUT (error), 1 = LOW, 2 = HIGH. Callers pass `on + 1`, so
"on" means drive the pin high.

**[O, Ghidra] Corrected 2026-10-12.** The driver in use is `gpioe_aw9110b_*` (see the I2C devices table), whose
pin → bit table is at **`0x3f4a8c18`** (`gpioe_aw9110b_set_level` `0x401c73bc`). The earlier map used the AW9523B
driver's identity table at `0x3f4a8c9e`, which this firmware never uses. "P0"/"P1" below are register bytes
0x00/0x02/0x04 and 0x01/0x03/0x05 (the AW9110B's ports A and B):

| Logical pin | Bit | Function | Evidence |
|---|---|---|---|
| 0 | **P1_0** | LED driver enable | [O] `<hal_enable_leds>` `0x401398e0`; **[O, hw]** driving it high lit the ring (ch0 = red) |
| 1 | **P1_1** | SD card power | [O] `_set_enabled` `0x4010e330` ("Turning SD card %s") |
| 2 | **P1_2** | Audio amplifier enable | [O] `_set_enabled` `0x40104ba8` (`audio_amp`) |
| 3 | **P1_3** | ID_OUT (drives a resistor divider for hardware-ID sensing) | [O, Ghidra] `<_enable_id_out>` `0x40139028` calls `set_level(dev, 3, on + 1)` and reads it back |
| 4–8 | **P0_0–P0_4** | Buttons (inputs, interrupt enabled) | [O] config table; **[O, hw]** see [Buttons](#buttons) |
| 9 | **P0_5** | Touch controller enable | [O] `<hal_enable_touch_controller>` `0x40139a78` |
| — | P0_6, P0_7, P1_4–P1_7 | Not part of the AW9110B model; **never changed by stock** | [O, Ghidra] config is read-modify-write, so they keep their power-on state (output, low) |

Boot config (`gpioe_aw9110b_config_pins` `0x401c70d4`, table `0x3f4a8b4c`: entries 0–3 and 9 = 1 → output, rest
→ input): CFG `P0 |= 0x1F` (buttons in), P1_0–P1_3 and P0_5 outputs; OUT for those pins low; interrupts enabled on
the button pins; GCR bit 4 set (port 0 push-pull).

**[O, hw]** Releasing **P0_6** (driving it high in open-drain mode) wedged the I2C bus within ~4 ms on 2026-10-12
(expander NACKs, then the board hangs until a full power-cycle). Releasing P0_5 for 30 s was harmless. The probe
now refuses to change P0_6, P0_7 and P1_4–P1_7, and its "button pins to INPUT" switch writes only `CFG0 |= 0x1F`.

Earlier hardware results, reinterpreted: "touch enable on P1_1 confirmed drivable" actually switched **SD power**;
"driving P0_0 high did not enable the LEDs" drove the volume-up button pin. Neither is evidence against the map
above.

### Buttons

`0x40139bb8` [O]. It only runs when the expander is at 0x58. It builds a button bitmask from the expander's input
byte (cached at `0x3ffd8843`, updated by `<hal_i2c_rx_callback>` `0x40138fdc` from the expander interrupt):

| Mask bit | Source | Active |
|---|---|---|
| 0 | expander input bit 0 | low |
| 2 | expander input bit 1 | low |
| 4 | expander input bit 2 | low |
| 1 | expander input bit 3 | low |
| 3 | expander input bit 4 | low |
| 6 | **GPIO33** ("TOUCH") | high |
| 5 | **GPIO36** ("CONNECT") | low |

"Expander input bit 0–4" = P0_0–P0_4 = logical pins 4–8 in the corrected map above, so there is no overlap with
the enable outputs (which are on P1).

The button task (`butt_task_main` `0x401372b4`) handles single taps, tap counts, long press → shutdown, the
nightlight button and the "swap" button.

**[O, hw] Confirmed on hardware (probe firmware).** The five front buttons are expander inputs **P0_0–P0_4,
active-low** (idle pulled high → `P0 = 0x1F`; each press pulls one bit to 0). They are only readable when the pins
are put in **input** mode — on this unit they default to *outputs* (reading a constant 0x00), so the stock
firmware must configure them as inputs before reading (it does: `CFG0 |= 0x1F`). The probe's "button pins to
INPUT" action does the same. Physical mapping, by pressing each button one at a time:

| Button | Expander bit |
|---|---|
| Volume up | P0_0 |
| Brightness up | P0_1 |
| Night light | P0_2 |
| Volume down | P0_3 |
| Brightness down | P0_4 |

Re-confirmed 2026-10-12: each button pressed twice on its own; only its own bit moved (`seen_toggling = 001F`). The
brightness rocker is two switches (up/down); pressing its middle fires both P0_1 and P0_4.

Not yet retested on hardware: `GPIO33` TOUCH (needs the touch-controller enable, expander P1_1) and `GPIO36`
CONNECT.

## Direct GPIOs (summary)

| GPIO | Use | Tag |
|---|---|---|
| 0 | I2C0 SDA (also a strapping pin) | [O] |
| 2 | SD D0 | [O] |
| 4 | LED PWM ch3 | [O] |
| 5 | LED PWM ch0 | [O] |
| 13 | I2C1 SDA (RIoT+) / variant sense | [O] |
| 14 | SD CLK | [O] |
| 15 | SD CMD | [O] |
| 18 | LED PWM ch1 | [O] |
| 19 | LED PWM ch2 | [O] |
| 21 | I2S DOUT | [O] |
| 22 | I2C0 SCL | [O] |
| 23 | I2C1 SCL (RIoT+) / variant sense | [O] |
| 25 | I2S WS/LRCLK | [O] |
| 26 | I2S BCLK | [O] |
| 27 | UART1 TX → display MCU | [O] |
| 32 | variant sense | [O] |
| 33 | TOUCH input (touch controller output) | [O] |
| 34 | GPIO-expander interrupt (input-only) | [O] `gpioe_config_interrupt` → `0x401c518c` with pin `0x22` |
| 35 | ADC1_CH7: hardware-ID voltage (RIoT) | [O] |
| 36 | CONNECT input (input-only); ADC1_CH0 sampled by the ULP | [O] |
| 39 | ADC1_CH3: battery / supply voltage (RIoT+) | [O] |

## Night-light LEDs (LEDC)

`<LED_task_main>` `0x4013af30` [O]. Configs are static structs in DRAM.

- **Timer** (`0x3ffbf6e0`): high-speed mode, timer 0, **12-bit** resolution, **19 500 Hz** (`0x4c2c`).
- **Channels** (`0x3ffbf650` + n×0x24):

| LEDC ch | GPIO | Colour **[O, hw]** | Duty scaling (16-bit input → 12-bit duty) | Effective max |
|---|---|---|---|---|
| 0 | **GPIO5** | red | `v >> 5` | 50 % |
| 1 | **GPIO18** | green | `(v * 3) >> 7` | 37.5 % |
| 2 | **GPIO19** | blue | `v >> 6` | 25 % |
| 3 | **GPIO4** | white | `v >> 6` | 25 % |

The scaling is in `0x4013a330`. It is bypassed when the hardware-info byte equals 5. Channels are faded with
`ledc_set_fade_with_time`.

[I] Channel order is **R, G, B, W**. The LED message's four u16 fields go to ch0..ch3 in order, and the routine
JSON uses `{"r","g","b","w"}` in that order. The driven *emitters* are confirmed by teardown: a separate LED ring
board (`SKT1841LWAAW 622042B-U5`) carries **12 RGB LEDs (D1–D12) + 12 white LEDs (W1–W12)** [O, hw] — so R/G/B
drive the colour LEDs in common and the 4th channel drives the whites. **[O, hw] Confirmed 2026-10-12:** each
channel lit on its own gives ch0 red, ch1 green, ch2 blue, ch3 white.

The LED driver also needs the expander **LED enable**, **P1_0** (logical pin 0), set high. **[O, hw]** With it low
the channels produce no light; with it high each channel lights. Switching P1_0 on makes the expander NACK for
~10–25 ms (even with every channel off), so writes right after it need spacing — see
[probe-firmware.md](probe-firmware.md).

RIoT (hw type 1) also sets up LEDC from stack-built configs in `hal_check_riot_hardware` (`0x40139606`). Those were
not decoded; check whether they override the table above on RIoT.

## Audio (I2S)

`_install` `0x40104f6c` (`audio_driver_i2s.c`) [O]. Uses the IDF 5 `i2s_std` driver, TX only:

| Field | Value |
|---|---|
| Sample rate | runtime (`0x3ffbe7cc`), from the file being played |
| Clock source | default (PLL_F160M) |
| MCLK multiple | 256 |
| Data bit width | 16 |
| Slot mode | runtime channel count (`0x3ffbe7c8`); mono → slot mask RIGHT (2), stereo → BOTH (3) |
| WS width | 16, Philips (bit_shift = 1) |
| **MCLK** | −1 (unused) |
| **BCLK** | **GPIO26** |
| **WS** | **GPIO25** |
| **DOUT** | **GPIO21** |
| DIN | −1 |

[I] There is no MCLK and no codec register setup, so this is a digital-input class-D amp (MAX98357A-style).
`AUD_Codec` / `AUD_SDEC` are ESP-ADF *software* decoders (WAV, MP3). The amp is powered via expander logical pin 2.

Sounds play from the SD card (`/mnt`). Files are downloaded from `images.hatchbaby.com` / `assets.ctfassets.net`
and cached by path (`audio_filepath_cache`).

## SD card

`<hatch_sd_init>` `0x4010e6a4` [O]:

- SDMMC host, slot config copied from `0x3f4823a4`, bus **width = 1**.
- Host flags `0x21`, max frequency **40 MHz**.
- Internal pull-ups enabled on **GPIO14 (CLK), GPIO15 (CMD), GPIO2 (D0)**.
- Powered via expander logical pin 1, then waits 1 s to stabilise.
- FAT, mounted at **`/mnt`**.

## Clock display

A separate display MCU on **UART1 TX = GPIO27**. See [display-protocol.md](display-protocol.md).

## ADC / power

- **RIoT:** `adc1_config_width(12 bit)`. Toggles ID_OUT, reads ADC1_CH7 (GPIO35) with ID_OUT off and on, and
  computes a hardware ID from the two voltages.
- **RIoT+:** reads ADC1_CH3 (GPIO39), then finds and checks the GreenPAK (bus 0). The power task
  (`power_task_*`) handles:
  - battery percentage via the GreenPAK comparator (it moves Vref to bracket the battery voltage);
  - two charge levels;
  - external and wireless 5 V detection;
  - the power button, critical-battery shutdown, and the wireless-power IC.
- **ULP:** `<init_ulp_program>` `0x40135958` loads a ULP program that samples ADC1_CH0 (GPIO36) for wake. It sets
  RTC IO on GPIO0, 2, 12 and 15.
