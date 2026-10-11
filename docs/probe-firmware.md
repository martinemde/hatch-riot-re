# Probe firmware: test plan

`esphome/hatch-riot-probe.yaml` is a **diagnostic** ESPHome firmware. It doesn't replace the stock firmware's
features. Its job is to confirm or disprove each item in [hardware-map.md](hardware-map.md) and
[open-questions.md](open-questions.md), with as little risk to the device as possible.

## Safety model

| Risk | Mitigation |
|---|---|
| Can't return to stock | `scripts/backup.sh` reads the whole flash **twice**, checks the hashes match, saves the eFuse summary and partition table. `scripts/restore.sh` writes it back and verifies. |
| Device is locked (flash encryption / secure boot) | `backup.sh` checks the eFuses and **stops** if either is on. Do not flash in that case. |
| Bricking via a wrong strapping state | GPIO0 (I2C SDA) and GPIO5 (LED PWM) are strapping pins, used the same way the stock firmware uses them. Download mode is still available via GPIO0 + EN. |
| Corrupting the GreenPAK (power/battery logic) | **No code path writes to it.** The firmware's only I2C write path is hard-wired to the expander address 0x58 and registers 0x02–0x05 / 0x11. The I2C scan sends zero-length address probes only. |
| Writing an unknown chip | Expander writes are refused unless its ID register reads `0x23` (AW9523B). An AW9110B board is read-only. |
| Driving an expander pin into a button (short to GND/VCC) | Writes need **"Expander writes armed"** (always off at boot). Any pin seen changing as an input is **permanently refused** for that boot. Writes auto-revert to the boot snapshot after **30 s**, and on disarm. Only one pin changes per action. |
| Over-driving LEDs | Each LEDC channel's `max_power` equals the stock firmware's own cap (50 / 37.5 / 25 / 25 %). All lights start OFF. |
| Loud audio / speaker damage | RTTTL beeps only, at 25 % gain, and only on button press. |
| RIoT+ battery behaviour | No power-latch, charger or deep-sleep code. **Keep RIoT+ units on external power** for the whole session. The second I2C bus is only added via `riot-plus.yaml`, scan only. |
| Display MCU | UART frames only; it is never written over I2C. |
| Unauthenticated re-flash over the network | API and OTA both use the encryption key from `secrets.yaml`; web-UI firmware upload is disabled (`web_server: ota: false`). |

## Prerequisites

1. Open the unit and find the ESP32 UART0 pads: TX = GPIO1, RX = GPIO3, plus GND, 3V3 (or keep the unit's own
   supply), EN and GPIO0.
2. Use a 3.3 V USB-UART adapter. **Do not power the board from the adapter if it's also on its own supply.**
3. Enter download mode: hold GPIO0 low, pulse EN.

## Procedure

### 0. Backup (mandatory)

```sh
scripts/backup.sh /dev/ttyUSB0
```

If it prints `STOP`, do not continue.

### 1. Flash the probe

```sh
cd esphome
cp secrets.yaml.example secrets.yaml   # fill in Wi-Fi etc.
../.venv-esphome/bin/esphome run hatch-riot-probe.yaml --device /dev/ttyUSB0
```

Keep the serial console open (`esphome logs … --device /dev/ttyUSB0`). Every probe result is logged there. A web
UI is also on `http://hatch-riot-probe.local/`.

### 2. Passive checks (no writes)

| Check | Expected (from static analysis) | Confirms |
|---|---|---|
| Boot log `Variant sense GPIO13/32/23` | all 0 → RIoT, all 1 → RIoT+ | variant detection |
| I2C scan on bus 0 | 0x58 ACK; probably 0x26 (accel), 0x70 (display) | I2C0 = GPIO0/22, addresses |
| Boot log `Expander ID=` | `0x23` → AW9523B | expander part |
| `Boot snapshot: OUT/CFG/GCR` | record these values | expander power-on state |
| Press each physical button, one at a time | one `P0_n` bit per button (expect P0_0..P0_4, active low); text sensor "bits seen toggling" grows | [open question 1](open-questions.md#1-expander-pin-overlap), button map |
| GPIO34 INT sensor | toggles low on button activity | expander INT pin |
| GPIO33 TOUCH | goes high on touch (may need touch-enable first, step 3) | TOUCH input |
| GPIO36 CONNECT | note the level with and without the power/base connected | CONNECT meaning |
| ADC GPIO35 / GPIO39 | record values | ID / supply sense |
| "Dump accelerometer 0x26" | non-`--` values; reg 0x01 = part ID | accelerometer address |
| RIoT+ only: add `riot-plus.yaml`, re-flash over OTA | bus 1 scan: 0x10 (GreenPAK), 0x60 (wireless power)? | I2C1 = GPIO13/23 |

Finish step 2 **before** arming writes. It fills the "seen toggling" guard with the real button pins.

### 3. Display (UART only)

1. "Display: all segments", then "Display text" `1234`.
2. If nothing shows: change "Display UART parity" to `NONE`, then `EVEN`, and retry. Record which one works
   (open question 3).
3. Step "Display misc bits" 0–7. All digits show `8`; note which colon or dots appear (open question 4).
4. Step "Display brightness level" 0–15.

### 4. LEDs

Turn on each "LED chN" light at low brightness. If they stay dark, the LED driver needs its enable line, so do
step 5 first and retry within the 30 s window. Record the colour of each channel (open question 2).

### 5. Expander outputs (guarded)

1. Turn on **"Expander writes armed"**. Don't press buttons while armed.
2. Press "Hypothesis: LED enable = P0_0 high", then within 30 s turn on an LED channel.
3. "Hypothesis: amp enable = P0_2 high", then within 30 s "Audio: short beep".
4. "Hypothesis: touch enable = P1_1 high", then touch the top and watch GPIO33.
5. If a hypothesis fails, try "Expander P0 push-pull (stock mode)" (the stock driver sets it), or step through
   other pins with "Expander test pin" + "Expander: drive test pin".
6. Turn "Expander writes armed" off when finished. This restores the boot snapshot.

If a "Hypothesis" button is refused because the pin was seen toggling as an input, that alone answers open
question 1: the pin is a button, not an enable line.

### 6. Back to stock (optional)

```sh
scripts/restore.sh /dev/ttyUSB0 backups/<mac>_<timestamp>
```

## Recording results

Update [hardware-map.md](hardware-map.md) (change `[I]` to `[O, hw]`) and close items in
[open-questions.md](open-questions.md). Include the serial log excerpt for each.
