# ESPHome firmware

- **`hatch-rest*.yaml`** — the working stock replacement for the Hatch Rest 2nd gen (RIoT). See
  [../docs/esphome-plan.md](../docs/esphome-plan.md#replacement-config) for what it does.
- **`hatch-riot-probe.yaml`** — a read-mostly diagnostic firmware for confirming the hardware map. Read
  [../docs/probe-firmware.md](../docs/probe-firmware.md) (safety model and test plan) **before flashing it**.

| File | Purpose |
|---|---|
| `hatch-rest-base.yaml` | **stock replacement** for the Hatch Rest 2nd gen (RIoT): night light, speaker (Home Assistant media + **Music Assistant via Sendspin**), buttons, touch, clock, status LEDs, toddler lock. Shared base; no `external_components` |
| `hatch-rest.yaml` | local build: base + components from `components/` |
| `hatch-rest-builder.yaml` | ESPHome Builder add-on: base + components pulled from GitHub (see [below](#esphome-builder-add-on)) |
| `hatch-riot-probe.yaml` | probe (diagnostic) config |
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
with `manual_ip:` for a static address). `main` follows every push to this repo; for a device you rely on, pin a tag
or commit in both the package URL (`…/hatch-rest-base.yaml@<ref>`) and `ref:`.

A first flash of a stock unit still has to go over serial (the stock firmware won't accept an ESPHome OTA image):
back up with `../scripts/backup.sh`, then flash `hatch-rest.yaml` or the Builder's "Manual download" image.

