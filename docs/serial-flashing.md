# Serial backup and first flash

The first ESPHome flash has to go over serial: the stock firmware won't accept a third-party image over the air.
The same connection is used to back up the stock flash first. After this, updates go over Wi-Fi and the wires can
come off.

Start with the main board out of the unit ([teardown.md](teardown.md), steps 1–11).

## Soldering to the module

**There is no usable header.** No breakout or test-pad cluster found on RIoT Main connects all of the required
pins (the through-holes next to the module don't), so the wires are soldered **directly to the ESP32-WROVER-E
module's edge pads**.

`TODO: add a photo of the wires soldered to the module.`

| Signal | Module pad (ESP32-WROVER-E pin) | Connect to |
|---|---|---|
| GND | 38 | adapter GND |
| 3V3 | 2 | 3.3 V supply (see [Power](#power)) |
| EN | 3 | a wire you can briefly touch to GND (reset) |
| IO0 | 25 | GND while resetting = download mode |
| TXD0 (GPIO1) | 35 | adapter **RX** |
| RXD0 (GPIO3) | 34 | adapter **TX** |

Pin numbers are from the
[ESP32-WROVER-E datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf)
pin layout: with the antenna end at the top and the shield facing you, pins 1–14 run down the left side, 15–24
along the bottom and 25–38 up the right side. Check against the datasheet before soldering.

What you need:

- A **3.3 V** USB-to-serial adapter (CP2102, CH340, FT232 or similar). Never 5 V logic.
- A fine-tip soldering iron, flux and thin wire (30 AWG wire-wrap wire works well), plus magnification.
- Kapton tape to hold the wires down so they don't pull the pads off.

Tips:

- Tin the wire ends first, then tack each one to the side of its pad with a little flux. The pads are 1.27 mm
  apart; check for bridges to the neighbouring pads before powering up.
- Use **GND pin 38**, not pin 1: pins 1–3 (GND, 3V3, EN) sit side by side, and three wires on adjacent pads are
  much harder to solder cleanly.
- IO0 is also the I2C SDA line on this board. Holding it low only matters at reset; release it to boot normally.

## Power

With the board out of the unit, nothing else powers it. Power it from the **adapter's 3.3 V pin** to the module's
3V3 pad; that was enough for the backup and the flash (Wi-Fi isn't running in download mode). If the adapter
browns out (reads failing or the chip resetting), use a separate 3.3 V supply with its GND tied to the adapter's
GND instead. Never connect two supplies to 3V3 at once.

## Download mode

This is done by hand (the adapter's DTR/RTS aren't wired for auto-reset):

1. Hold **IO0 to GND**.
2. Briefly touch **EN to GND** (or power up) and release it.
3. Release IO0 (or keep it grounded for the whole session; that's fine too).

Check the connection; this only reads:

```sh
.venv-esphome/bin/esptool --port /dev/ttyUSB0 chip-id
```

It should print the chip type (ESP32-D0WD-V3) and the MAC address. Set up `.venv-esphome` as in
[esphome/README.md](../esphome/README.md#build) if you haven't.

## 1. Back up the stock flash (mandatory)

```sh
scripts/backup.sh /dev/ttyUSB0
```

This never writes to the device. It checks the eFuses (and stops if flash encryption or secure boot is on), reads
the whole 16 MB flash twice and compares the reads, and saves everything to `backups/<mac>_<timestamp>/`. If it
prints `STOP`, don't continue.

Each read takes about 6–8 minutes. If the second one fails, verify the first with one more read at a lower baud
instead of starting over:

```sh
scripts/backup.sh --verify-existing backups/<mac>_<timestamp> /dev/ttyUSB0 115200
```

Keep the backup safe: it holds the unit's device certificates and Wi-Fi credentials, and it's the only way back
to stock.

## 2. Flash ESPHome

Put the module back in download mode, then either build and flash from this repo:

```sh
cd esphome
../.venv-esphome/bin/esphome run hatch-rest.yaml --device /dev/ttyUSB0
```

or write the ESPHome Builder's **Manual download** image ("Factory format"):

```sh
.venv-esphome/bin/esptool --port /dev/ttyUSB0 write-flash 0x0 hatch-rest.factory.bin
```

Then release IO0 and reset (touch EN to GND). The device boots ESPHome and joins Wi-Fi; watch it with
`esphome logs hatch-rest.yaml --device /dev/ttyUSB0`.

## 3. Finish

Once it's on Wi-Fi and Home Assistant sees it, later updates go over the air
([esphome/README.md](../esphome/README.md)). Remove the wires and reassemble ([teardown.md](teardown.md#reassembly)).

## Going back to stock

With the same wiring and download mode:

```sh
scripts/restore.sh /dev/ttyUSB0 backups/<mac>_<timestamp>
```

It checks the backup's checksum and that the MAC matches, asks for confirmation, then writes and verifies the
whole flash.
