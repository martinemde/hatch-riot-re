# ESPHome firmware

- **`hatch-rest*.yaml`** — the working stock replacement for the Hatch Rest 2nd gen (RIoT). See
  [../docs/esphome-plan.md](../docs/esphome-plan.md#replacement-config) for what it does.
- **`hatch-riot-probe.yaml`** — a read-mostly diagnostic firmware for confirming the hardware map. Read
  [../docs/probe-firmware.md](../docs/probe-firmware.md) (safety model and test plan) **before flashing it**.

| File | Purpose |
|---|---|
| `hatch-rest-base.yaml` | **stock replacement** for the Hatch Rest 2nd gen (RIoT): night light, speaker (Home Assistant media + **Music Assistant via Sendspin**), buttons, touch, clock (or custom text), status LEDs, toddler lock, Bluetooth Wi-Fi setup (Improv). Shared base; no `external_components` |
| `hatch-rest.yaml` | local build: base + components from `components/` |
| `hatch-rest-builder.yaml` | ESPHome Builder add-on: base + components pulled from GitHub (see [below](#esphome-builder-add-on)) |
| `hatch-riot-probe.yaml` | probe (diagnostic) config |
| `bluetooth-proxy.yaml` | optional add-on: Home Assistant Bluetooth proxy (see [below](#bluetooth)) |
| `riot-plus.yaml` | adds I2C bus 1 (scan only) for RIoT+ boards |
| `components/riot_probe/` | local external component: variant detect, expander polling, guarded writes, register dumps |
| `components/hatch_expander/` | GPIO expander at 0x58 as ESPHome pins (stock AW9110B pin model, bit-banged I2C, verified writes, INT-driven reads) |
| `components/hatch_display/` | clock display over UART1 (stock font, colon, inverted brightness) |
| `components/hatch_status_led/` | Wi-Fi / lock bicolour status LEDs (off / green / red / amber, driven like stock) |
| `secrets.yaml.example` | copy to `secrets.yaml` |

## Build

```sh
python3 -m venv ../.venv-esphome && ../.venv-esphome/bin/pip install esphome   # tested with 2026.9.1
../.venv-esphome/bin/pip install 'bitstring>=4,<5'   # see note below
cp secrets.yaml.example secrets.yaml && $EDITOR secrets.yaml
../.venv-esphome/bin/esphome compile hatch-riot-probe.yaml
```

> **`bitstring` pin:** `esptool`/`espefuse` 5.3.x import `BitStream`, which `bitstring` 5.0.0 removed, while
> esptool's own requirement (`bitstring>=3.1.6`) leaves the upper bound open — so a fresh install can pull the
> broken 5.0.0. Symptom: `scripts/backup.sh` dies at the eFuse step with
> `ImportError: cannot import name 'BitStream' from 'bitstring'`. Fix: `pip install 'bitstring>=4,<5'`.

Flash only after `../scripts/backup.sh` succeeds.

## ESPHome Builder add-on

To manage the replacement firmware from the Home Assistant **ESPHome Builder** add-on, use
[`hatch-rest-builder.yaml`](hatch-rest-builder.yaml). It pulls both the config (`hatch-rest-base.yaml`, as a remote
package) and the custom components from this GitHub repo, so the Builder only holds this short file.

1. In the Builder, open **Secrets** and add `wifi_ssid`, `wifi_password`, `fallback_ap_password` and
   `api_encryption_key`. Use the **same `api_encryption_key`** the device was last flashed with: it secures both the
   API and OTA, so the first wireless install only works if it matches (and Home Assistant keeps its pairing).
2. Create a device and replace its YAML with the contents of `hatch-rest-builder.yaml`. Keep `name: hatch-rest` to
   adopt a unit already running this firmware.
3. **Install → Wirelessly.** Updates keep bootloader rollback.

Customise by adding keys after `packages:` — they override the base (for example `substitutions:`, or a `wifi:` block
with `manual_ip:` for a static address). The file is pinned to release **v1.2**; to update, change the tag in both the
package URL (`…/hatch-rest-base.yaml@<ref>`) and `ref:` (or use `main` to follow every push).

A first flash of a stock unit still has to go over serial (the stock firmware won't accept an ESPHome OTA image):
back up with `../scripts/backup.sh`, then flash `hatch-rest.yaml` or the Builder's "Manual download" image.

## Bluetooth

### Wi-Fi setup over Bluetooth (Improv)

Built into the base config. When the Hatch can't join Wi-Fi (new network, changed password), it advertises over
Bluetooth, and the Home Assistant companion app or [improv-wifi.com](https://www.improv-wifi.com/) can send it new
credentials. **Hold the bottom CONNECT button** to authorize, like the stock pairing flow; authorization lasts one
minute. It's idle while Wi-Fi is connected. The fallback access point (`Hatch Rest Fallback`) still works as before.

### Bluetooth proxy (optional)

[`bluetooth-proxy.yaml`](bluetooth-proxy.yaml) makes the Hatch a
[Bluetooth proxy](https://esphome.io/components/bluetooth_proxy/) for Home Assistant, including active connections
(locks, sensors that need GATT reads). Add it as a second package.

Local build (`hatch-rest.yaml`):

```yaml
packages:
  hatch_rest: !include hatch-rest-base.yaml
  bluetooth_proxy: !include bluetooth-proxy.yaml
```

ESPHome Builder (`hatch-rest-builder.yaml`), with the same `@<ref>` as the base package:

```yaml
packages:
  hatch_rest: github://w1ll1am23/hatch-riot-re/esphome/hatch-rest-base.yaml@<ref>
  bluetooth_proxy: github://w1ll1am23/hatch-riot-re/esphome/bluetooth-proxy.yaml@<ref>
```

> **Known issue: audio stutters with the proxy on.** The ESP32 has one 2.4 GHz radio shared by Wi-Fi and Bluetooth,
> and BLE scanning takes airtime from Wi-Fi. Tested on hardware (2026-10-14, ESPHome 2026.9.1, default scan
> settings): streamed audio (Sendspin / Home Assistant media) stutters. Only enable the proxy on a unit you don't
> use as a speaker. That's why it's off by default.

Scan settings are left at ESPHome's defaults on purpose. Don't copy the 1100 ms
`interval`/`window` from older proxy guides: long scan windows can drop Wi-Fi on some access points.
