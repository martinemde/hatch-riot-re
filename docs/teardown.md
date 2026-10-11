# Teardown

Photos of a disassembled unit (in `teardown/`), taken 2026-10-11. These confirm several things that were only
inferred from the firmware — most importantly the device identity and the exact Wi-Fi module.

## What the photos confirm

- **Device identity:** model **HBREST2** = Hatch **Rest (2nd gen)**. FCC ID `2AFYZ-HBREST2`, IC `23920-HBREST2`,
  input `5V⎓1A`. The main board silkscreen reads **"RIoT Main V1.5", dated 2021-06-5**. So the firmware's
  "RIoT" is the Rest 2nd gen (previously a guess — now confirmed).
- **MCU module:** **ESP32-WROVER-E** (clearly printed on the can), with the **PCB antenna** variant (serpentine
  trace at the module end, no U.FL connector). Module FCC ID `2AFYZ-ESP32WROVERE`. This settles the
  chip-replacement spec: an ESP32-WROVER-E, PCB-antenna version. See
  [esphome-plan.md](esphome-plan.md#replacing-the-module).
- **LEDs:** a separate LED ring board (marking `SKT1841LWAAW 622042B-U5`) with **12 RGB LEDs (D1–D12) and 12
  white LEDs (W1–W12)**. That matches the four LEDC channels in the firmware: R/G/B drive the colour LEDs in
  common, the 4th channel drives the separate whites.
- **Candidate serial/programming header:** a cluster of plated through-holes next to the module on RIoT Main
  (photo 11). Worth metering for UART0 TX/RX, EN, IO0, GND, 3V3 before scraping for test points — this is likely
  the easiest manual-flash access point.

## Images

| # | File | Shows |
|---|---|---|
| 01 | `teardown/01-*.jpg` | Base label: `Hatch MODEL:HBREST2`, FCC/IC IDs, `5V 1A`, Wi-Fi + lock icons |
| 02 | `teardown/02-*.jpg` | Same label, screws visible (base opened) |
| 03 | `teardown/03-*.jpg` | Base plate with small **"RIoT_B…" (RIoT Base) board**, a tactile switch and a connector |
| 04 | `teardown/04-*.jpg` | Base interior; 2-pin power post and an inter-board pin header |
| 05 | `teardown/05-*.jpg` | RIoT Base board removed — pin header `J2`, date code `2217` (wk17 2022) |
| 06 | `teardown/06-*.jpg` | Top shell exterior buttons (rocker + round button) |
| 07 | `teardown/07-*.jpg` | Charging/dock board in the foot: power contacts, TVS parts (`U5`, `TVS*`) |
| 08 | `teardown/08-*.jpg` | Center post: 2-pin power connector + a 3-pin JST (red/green/black) |
| 09 | `teardown/09-*.jpg` | **LED ring board** `SKT1841LWAAW 622042B-U5`: D1–D12 (RGB) + W1–W12 (white), switches S1–S3 |
| 10 | `teardown/10-*.jpg` | **RIoT Main V1.5** board, component side: ESP32-WROVER-E module, inter-board header, S4/S5 |
| 11 | `teardown/11-*.jpg` | Close-up: **ESP32-WROVER-E** module marking + PCB antenna; candidate through-hole header nearby |

## Still to read off the board (not legible in these photos)

- **Flash size** (N4/N8/N16): not printed on the WROVER-E can — read it with `esptool … flash-id`. (The app
  image header says 16 MB; verify.)
- **Accelerometer / sensor part numbers** on RIoT Main (`U*` chips) — need sharper close-ups.
- **Whether the "RIoT Base" board carries the GreenPAK.** The firmware talks about a "base board GreenPAK"; a
  clear shot of the `RIoT_B…` board's ICs would confirm it on this (non-Plus) unit. See
  [open-questions.md](open-questions.md).
