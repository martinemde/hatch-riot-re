# Local Rest+ WSUP flasher

Build with your own secrets, then serve the **application-only** binary from a
computer on the same reachable LAN as the Hatch. This server uses that computer's
Bluetooth through Bleak; the browser does not need Web Bluetooth.

## Build

From the repository root (Python3.13 and `uv`):

```sh
uv venv .venv-flasher
uv pip install --python .venv-flasher/bin/python esphome==2026.9.1 -r flasher/requirements.txt
cp esphome/secrets.yaml.example esphome/secrets.yaml
# Edit secrets.yaml: your Wi-Fi credentials and a unique 32-byte base64 key.
.venv-flasher/bin/esphome compile esphome/hatch-plus-example.yaml
```

Never commit secrets or share the compiled binary: it embeds your credentials.
Keep an encrypted backup of the YAML/secrets for future OTA updates.

## Run

Replace the address below with this computer's LAN IPv4 address, reachable from
Hatch. Do not use localhost, a VPN address or an isolated VLAN address.

```sh
.venv-flasher/bin/python flasher/server.py \
  --esphome esphome/.esphome/build/hatch-plus-example/build/firmware.ota.bin \
  --advertise 192.168.1.20
```

Open http://127.0.0.1:8000 on the same computer. Grant Bluetooth permission if
asked. Allow inbound TCP8000 from Hatch through the host/network firewall. Keep
Hatch on stable power. Briefly hold its bottom button about2seconds to open setup
mode; do not factory reset. Click Scan, select the correct RIOT Plus device, then
Install ESPHome and confirm. Scan reads candidate GATT services because Hatch may
omit its WSUP service from advertisements. Scanning does not send update commands.

Firmware is available on LAN only through a temporary unguessable URL after an
explicit flash command; controls are localhost-only. Do not port-forward this
server. Stop it with Ctrl-C after use. Python/Bleak OS support and network policies
vary; the original BLE/HTTP path was tested on macOS with one Rest+ hardware8.0.2.

Progress measures bytes written to the HTTP connection, **not flash completion**.
BLE OK means command accepted, not successful update. Confirm independently that
ESPHome appears in HA and authenticates with your key. Future updates use:

```sh
.venv-flasher/bin/esphome run esphome/hatch-plus-example.yaml \
  --device hatch-plus-example-<mac-suffix>.local --no-logs
```

## Limits and recovery

This is experimental HBRESTPLUS2 support, not evidence that every Hatch model is
compatible. Battery/charging, physical controls and sound loops need hardware
validation. Do not flash a factory/merged image or an arbitrary stock revision.
Know your target OTA slot capacity and recovery options before first flash.
Stock WSUP can partially overwrite the inactive slot before rejecting oversized
images. There is no verified full-flash backup or guaranteed rollback here.

Optional `--stock /path/to/archived-stock-application.bin` enables Restore stock.
It **only works while a WSUP-capable application is running**, not after ESPHome
replaces it. This is not a bootloader recovery feature. UART/teardown may be needed
if firmware cannot boot or connect. Stock binaries are not redistributed.

GHA builds are compile checks only, with disposable CI credentials. They are not
installable public firmware and are deliberately not published as artifacts.
