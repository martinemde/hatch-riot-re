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
| Accelerometer, MiraMEMS **DA213** (ID `0x13`) | **0x27** (firmware tries 0x26, 0x27, 0x62) | [O] `<accelerometer_init>` `0x400ff590`: soft reset `0x00 ← 0x24`; at 0x27 → `accelerometer_da213_setup` `0x400ffac0` writes `0x0F–0x11 = 00 03 06`. **[O, hw]** ID `0x13`; stock setup + axis reads work (below). | **Orientation only**: the DA213 driver has no tap support; tap detection exists only for the 0x26 parts. See [Accelerometer](#accelerometer-da213) and [open-questions.md #6](open-questions.md). |
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
| 1 | **P1_1** | SD card power | [O] `_set_enabled` `0x4010e330` ("Turning SD card %s"); **[O, hw]** high → card answers init, low → no response |
| 2 | **P1_2** | Audio amplifier enable | [O] `_set_enabled` `0x40104ba8` (`audio_amp`); **[O, hw]** high → chime audible, low → same I2S playback silent |
| 3 | **P1_3** | ID_OUT (drives a resistor divider for hardware-ID sensing) | [O, Ghidra] `<_enable_id_out>` `0x40139028` calls `set_level(dev, 3, on + 1)` and reads it back; **[O, hw]** high → GPIO35 0.14 V → 1.67 V |
| 4–8 | **P0_0–P0_4** | Buttons (inputs, interrupt enabled) | [O] config table; **[O, hw]** see [Buttons](#buttons) |
| 9 | **P0_5** | Touch controller enable | [O] `<hal_enable_touch_controller>` `0x40139a78`; **[O, hw]** needs P0 push-pull (GCR bit 4): high → touches show on `GPIO33` |
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
| 5 | **GPIO36** ("CONNECT") = **bottom button** [O, hw] | low |

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

**[O, hw]** `GPIO33` TOUCH goes high while the top is touched, once the touch controller is enabled (P0_5 high
with port 0 push-pull; see [open-questions.md #12](open-questions.md)).

**[O, hw] 2026-10-12:** `GPIO36` "CONNECT" is the **button on the bottom of the unit**: idle high, low while
pressed (two presses, two clean low pulses; no expander bit moved).

**[O, Ghidra] What it does:** `GPIO_task1_service` `0x40138b68` turns the mask from `0x40139bb8` into button-event
IDs (debounced: a bit must match its previous sample before a change is queued):

| Mask bit | Input | Button ID |
|---|---|---|
| 0 | P0_0 volume up | 4 |
| 1 | P0_3 volume down | 3 |
| 2 | P0_1 brightness up | 1 |
| 3 | P0_4 brightness down | 2 |
| 4 | P0_2 night light | 9 |
| 5 | **GPIO36 bottom button** | **7** |
| 6 | GPIO33 touch (top) | 5 |

In `butt_task_main` `0x401372b4`, button 7 has no short-press action. **Holding it for more than 2 s logs
"BT triggered"** and starts Bluetooth setup: it calls `0x4010cde4`, queues a message, switches a mode via
`0x40103550(1)` and sets a 600 s timeout (`+0x3a8 = 600`) — i.e. the BLE pairing / Wi-Fi provisioning window, hence
"CONNECT". The same check also fires while volume down (ID 3) is held, as long as brightness down (ID 2) isn't.
The "Shutting down due to %d" long-press paths are IDs **5 (touch top, >1 s)** and 0xB, not this button; the
"Power button pressed for 2/5 seconds" strings belong to the RIoT+ `power_task`. Not checked on stock hardware.

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
| 36 | CONNECT = bottom button, active low (input-only); ADC1_CH0 sampled by the ULP | [O]; **[O, hw]** button |
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
`AUD_Codec` / `AUD_SDEC` are ESP-ADF *software* decoders (WAV, MP3). The amp is enabled by expander logical pin 2
= **P1_2**.

**[O, hw] Confirmed 2026-10-12** (probe: I2S BCLK 26 / WS 25 / DOUT 21, 16 kHz 16-bit mono, RTTTL at 25 % gain):
with P1_2 high the chime played audibly (3 plays, clean); ~12 s after the auto-revert set P1_2 low again, the same
playback ran to completion on I2S but was **silent**. So I2S on these pins reaches the amp, and P1_2 gates it.
Switching P1_2 on shows the same ~10–25 ms expander NACK window as the LED enable (succeeded on write attempt 3).

Sounds play from the SD card (`/mnt`). Files are downloaded from `images.hatchbaby.com` / `assets.ctfassets.net`
and cached by path (`audio_filepath_cache`).

## SD card

`<hatch_sd_init>` `0x4010e6a4` [O]:

- SDMMC host, slot config copied from `0x3f4823a4`, bus **width = 1**.
- Host flags `0x21`, max frequency **40 MHz**.
- Internal pull-ups enabled on **GPIO14 (CLK), GPIO15 (CMD), GPIO2 (D0)**.
- Powered via expander logical pin 1 (**P1_1**), then waits 1 s to stabilise.
- FAT, mounted at **`/mnt`**.

**[O, hw] 2026-10-12** (probe, card init + registers only, no mount, 20 MHz): with P1_1 **low** the card does not
respond (`ESP_ERR_INVALID_RESPONSE` after 4 ms). With P1_1 **high** (~18 s settle) init succeeds in 210 ms:
**SDHC/SDXC, CID name `SS16G`, 15193 MB (16 GB)**, OCR `0xC0FF8000`, 20 MHz. Switching P1_1 on caused no expander
NACKs.

## Accelerometer (DA213)

Read: 6 bytes from reg `0x02` (X, Y, Z little-endian int16), each `/ 4` → 14-bit counts, **4096 counts = 1 g**
(±2 g) [O] `0x400ffa50`. "Is upright" [O] `0x400ffb1c`: **Z < −0x7FF** (≈ −0.5 g).

**[O, hw] 2026-10-12** (probe ran the stock setup, then read once a second while the unit was turned):

| Position | X | Y | Z | Stock "upright" |
|---|---|---|---|---|
| Sitting normally | 0.0 | −0.05 | **−1.02 g** | yes |
| On one side / the other | **−1.0** / **+1.0** | ~−0.3 | ~0 | no |
| Upside down | 0.0 | −0.1 | **+1.0 g** | no |
| Tilted face-forward / face-back | ~0 | **+0.9** / **−1.05** | ~0 | no |

So Z points up out of the base, and "upright" means sitting normally (any tilt past ~60° clears it).

**[O, Ghidra] What uses it:** `hal_call_every_second` reads the axes every second, and the periodic status/clock
updater `0x4013866c` calls `accelerometer_is_device_upright` and queues a **type-2 message to the segment-display
queue** (`0x3ffd883c`, handled in `GPIO_task1_service`):

- **upright → status bits = 0** (all indicators off);
- **not upright → status bits = `0x40134574()`**: bit 0/4 from the connection state (event group + `0x40132a20`),
  bit 2 when a counter at `+0x3ac` is non-zero, bits 1/5 battery level (RIoT+ only).

`GPIO_task1_service` turns bit pairs (0/4, 1/5, 2/6) into three indicator states 0–3 for `0x40139c34`, which on
RIoT drives **LEDC low-speed channels 1 and 2** (indicators 0 and 2; indicator 1 is RIoT+-only). So the small
**status LEDs only show while the unit is tipped over / upside down** — presumably they sit on the bottom, next to
the bottom (CONNECT) button. In factory-test mode (`0x401198b8`/`0x4011987c` toggle a flag) the same check instead
sends a type-2 message to the LED task to light one of two colours by orientation.

See [Status LEDs](#status-leds-riot) for the pins (**[O, hw]** confirmed: Wi-Fi icon on GPIO13, toddler-lock icon on
GPIO32, shared red on GPIO23). The upright gating itself was not checked on stock firmware.

## Status LEDs (RIoT)

**[O, Ghidra]** `hal_check_riot_hardware` (RIoT branch, `*0x3ffd8844 == 1`) configures a **low-speed LEDC timer 0,
12-bit, 171 Hz** and three channels from stack-built configs — on the same pins RIoT+ uses for I2C1 and variant
sense:

| LEDC (low-speed) ch | GPIO | Initial duty | Driven by | **[O, hw]** LED |
|---|---|---|---|---|
| 0 | **GPIO23** | 0x800 | **nothing** — fixed 50 % reference for both indicators | common side of both bicolour LEDs |
| 1 | **GPIO13** | 0x800 | indicator 0 (connection state) via `0x40139c34` | **Wi-Fi** icon bicolour LED |
| 2 | **GPIO32** | 0x800 | indicator 2 (`+0x3ac` flag) via `0x40139c34` | **lock** icon bicolour LED |

**[O, hw] 2026-10-12:** three LEDs behind two icon windows on the case: a **Wi-Fi** icon and a **lock** icon (the
toddler lock, which disables the hardware buttons). Driving one pin alone (others low): GPIO23 lights **both**
windows red; GPIO13 lights the Wi-Fi window green; GPIO32 lights the lock window green.

**Wiring [O, hw]:** each window is a **red/green bicolour LED wired anti-parallel between GPIO23 and its own pin**
(GPIO13 = Wi-Fi, GPIO32 = lock): current GPIO23 → pin lights red, pin → GPIO23 lights green. Proof: GPIO23 and
GPIO13 both driven at the same in-phase 50 % PWM → the Wi-Fi window goes **dark** (no voltage across it) while the
lock window stays red.

**How stock drives it [O, Ghidra]:** nothing ever changes GPIO23 after boot (no `ledc_set_duty*`, fade or GPIO
call on low-speed ch0 / pin 23 on RIoT), so it is a constant 50 % square wave. `0x40139c34` sets the indicator pin's
duty and phase (hpoint) against it, with `*0x40110d28 = 0x800`:

| Indicator state | Pin duty, hpoint | vs GPIO23 | Colour |
|---|---|---|---|
| 0 | 0x800, 0 | in phase | **off** |
| 1 | 0x1000 (100 %), 0 | high while 23 is low | **green** |
| 2 | 0, 0 | low while 23 is high | **red** |
| 3 | 0x800, 0x800 | anti-phase | **red + green = amber** |

From the status bits (`GPIO_task1_service`): bit *n* alone → red, bit *n+4* alone → green, both → amber, none → off
(n = 0 Wi-Fi, 2 lock). **[O, hw] 2026-10-12:** the probe reproduces the stock scheme (low-speed LEDC timer 0,
12-bit, 171 Hz, GPIO23 fixed at 0x800; "Status: Wi-Fi/lock window" selects) and all four states — off, green, red,
**amber** — work in both windows. So indicator 2's `+0x3ac` flag is the **toddler-lock** state, and the stock
firmware shows Wi-Fi and lock status only while the unit is not upright.

Indicator state → `ledc_set_duty_with_hpoint` (duty, hpoint): 0 → (`*0x40110d28`, 0), 1 → (0x1000, 0),
2 → (0, 0), 3 → (`*0x40110d28`, `*0x40110d28`) with `*0x40110d28 = 0x800` — see the colour table above.
On RIoT+ the same indicators are instead written as codes `0x17/0x11/0x10` (not traced). Shown only when the unit is
not upright (see [Accelerometer](#accelerometer-da213)). Pins and colours confirmed on hardware with the probe's
"Status LED" lights; the stock state → duty mapping is not.

## Clock display

A separate display MCU on **UART1 TX = GPIO27**. See [display-protocol.md](display-protocol.md).

## ADC / power

- **RIoT:** `adc1_config_width(12 bit)`. Toggles ID_OUT, reads ADC1_CH7 (GPIO35) with ID_OUT off and on, and
  computes a hardware ID from the two voltages.
  **[O, hw] 2026-10-12** (this RIoT, 12 dB attenuation, ESPHome-calibrated): ID_OUT (P1_3) **low → GPIO35 =
  0.14 V**, **high → 1.67 V** (steady over 6 samples; back to 0.14 V after release). 1.67 V is ≈ half of 3.3 V, so the
  divider is roughly 1:1; 0.14 V is near the ADC's floor at this attenuation (≈ 0 V). GPIO39 read a steady 3.00 V
  throughout.

### Hardware version / ID

**[O, Ghidra]** `hal_update_hardware` `0x40139874` calls `hal_check_riot_hardware(7)`, which returns a version
`major.minor.id`, logs `HW v%d.%d.%d`, and stores it at `0x3ffbdec0 + 0x11..0x13`:

- **major** = 7 on RIoT, 8 on RIoT+ (variant from the GPIO13/32/23 sense). **minor** = 0.
- **id**: GPIO35 is read raw with ID_OUT off, then on (`esp_adc_cal`, 11 dB). `0x401d6da8(v_off, v_on)` looks the
  voltage up in a range tree — **v_on** in tree `0x3f4ac110` if v_on ≤ 2999 mV, otherwise **v_off** in tree
  `0x3ffc05e4`:

| v_on (mV) | id | | v_off (mV), when v_on > 2999 | id |
|---|---|---|---|---|
| 18–282 | 4 | | 18–282 | 8 |
| 318–582 | 3 | | 379–643 | 15 |
| 720–984 | 2 | | 772–1036 | 14 |
| 1111–1375 | 1 | | 1154–1418 | 13 |
| **1518–1782** | **0** | | (1571–1835?) | (12?) |
| 1925–2189 | 7 | | 1952–2216 | 11 |
| 2316–2580 | 6 | | 2334–2598 | 10 |
| 2718–2982 | 5 | | 2727–2991 | 9 |

  (The second tree walked as 11 twice with no 12; the 12 row is inferred from the spacing — one node was likely
  misread.) **This unit: v_on = 1.67 V → id 0 → `HW v7.0.0`.**

**Uses of the version:**

| Where | Use |
|---|---|
| `_is_nuvoton` `0x40104f24` (from `_audio_decoder_impl`) | **amplifier channel count**: minor 0 → id 0 = **1 channel (mono)**, id ≠ 0 = 2; other minors warn "Unknown amplifier channel count" and use 2. Sets the I2S slot mode (mono → RIGHT slot, stereo → BOTH). |
| `0x40134460` | formats `"%d.%d.%d"` into the device state (`+0x15f`) when it changes — the hardware version reported to the cloud/app |
| `0x4013a330` (LED duty scaling) | scaling skipped when major = 5 |
| `0x40139d14` (product type getter) | type 5 with id 8–15 is reported as type 7 |

So on the Rest 2nd gen the ID's main effect is choosing a **mono** amp (matching the single speaker) and reporting
`7.0.0`.
- **RIoT+:** reads ADC1_CH3 (GPIO39), then finds and checks the GreenPAK (bus 0). The power task
  (`power_task_*`) handles:
  - battery percentage via the GreenPAK comparator (it moves Vref to bracket the battery voltage);
  - two charge levels;
  - external and wireless 5 V detection;
  - the power button, critical-battery shutdown, and the wireless-power IC.
- **ULP:** `<init_ulp_program>` `0x40135958` loads a ULP program that samples ADC1_CH0 (GPIO36) for wake. It sets
  RTC IO on GPIO0, 2, 12 and 15.
