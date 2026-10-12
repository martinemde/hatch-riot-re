# ESPHome probe firmware

`hatch-riot-probe.yaml` is a read-mostly diagnostic firmware for confirming the hardware map. Read
[../docs/probe-firmware.md](../docs/probe-firmware.md) (safety model and test plan) **before flashing**.

| File | Purpose |
|---|---|
| `hatch-riot-probe.yaml` | main config |
| `riot-plus.yaml` | adds I2C bus 1 (scan only) for RIoT+ boards |
| `components/riot_probe/` | local external component: variant detect, expander polling, guarded writes, register dumps |
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
