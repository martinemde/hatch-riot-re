# Local Rest+ BLE flasher

Experimental local Wi-Fi provisioning and first-install tool for the Hatch
**Rest+ (2nd gen), HBRESTPLUS2** running stock firmware. Tested on macOS with
stock **7.1.645 / hardware 8.0.2** and **7.1.503 / hardware 8.0.1**.
**Rest (HBREST2), other generations, and other stock revisions are not verified.**
A matching BLE service or a manual protocol override does not establish support.

This directory runs independently of the ESPHome components in this repository.
It does not include or build Rest+ firmware. Supply an application-only image
built for your exact hardware; **do not use the repository's Rest YAML on Rest+**.
The BLE transport cannot validate a firmware's pin mapping or hardware support.

## Install

From the repository root, with Python 3.13 and `uv`:

```sh
uv venv --python 3.13 .venv-flasher
uv pip install --python .venv-flasher/bin/python -r flasher/requirements.txt
.venv-flasher/bin/python flasher/server.py --help
```

Bleak is the only runtime dependency; ESPHome is not required to run this tool.
A working Bluetooth adapter, OS Bluetooth permissions, and LAN access from the
Hatch to the server are required. Other operating systems are not hardware-tested.
The dashboard uses the browser's built-in APIs and has no npm dependencies.

## Read-only version probe and update protocol

After Scan and explicit target selection, click **Probe device**. This reads
standard BLE model, firmware and hardware characteristics only; it sends no
setup, authentication, registration, reset or update command. Flashing requires
a matching probe for the selected target and an automatic or explicit protocol choice. The version/model are read again
on the flash connection; a changed result requires review and reconfirmation.

Automatic selection is deliberately limited to analyzed **RIOT Plus** versions:

- **7.1.645:** `WSUP` URL command (proven on the original hardware 8.0.2 unit).
- **7.1.503:** BLE JSON `state.rF` request, using the image's actual ESP
  application descriptor version, `i:true`, and temporary URL. Proven complete
  ESPHome install on hardware 8.0.1, confirmed by the operator.
  The first attempt returned `OK` without downloading; one captured retry
  downloaded the full image and rebooted. The first failure remains unexplained.
  Wi-Fi command success does not establish authorization; this route has a
  separate BLE access gate. Official provisioning was not needed on this unit.
- Other versions/models: no automatic recommendation. Choose WSUP or legacy
  state.rF manually after probing. This does not establish compatibility.

The protocol selector also permits overriding a known version's recommendation.
The confirmation shows the chosen protocol. A device rejection (for example
`E01`) is displayed as an error and stops observation; no automatic fallback is
sent. E01 can mean unsupported command, denied access, or invalid parameters,
not conclusive firmware identification. Select another protocol and confirm a
new attempt explicitly if desired.

The probe selects a **transport**, not a firmware image, and does not prove
hardware compatibility, OTA partition fit, authorization or network access.
Both protocols can start a real install after explicit confirmation. An `OK`
reply is not successful download/flash/boot. The flasher observes for 180 seconds
and keeps the temporary URL valid for 300 seconds, matching the successful retry.
Stock 503 has a 70-second startup delay and a 10-second queue quiet period;
IP readiness/disconnection can delay processing further. No automatic retries,
registration, reset, or auth-key writes are performed. Confirm ESPHome boot in
Home Assistant after download. A reboot alone is not boot verification.

Build with your own secrets, then serve the **application-only** binary from a
computer on the same reachable LAN as the Hatch. This server uses that computer's
Bluetooth through Bleak; the browser does not need Web Bluetooth.

## Provision stock Wi-Fi only (no firmware image required)

Start the server with the computer’s LAN IPv4 address, then open
http://127.0.0.1:8000 **on that computer**:

```sh
.venv-flasher/bin/python flasher/server.py --advertise 192.168.1.20
```

Install the dependencies above first if needed. This mode needs no build or image;
firmware buttons stay disabled. Nothing connects to BLE on server startup.
Do not port-forward the server or expose its control endpoints over LAN.

1. Physically identify the intended stock unit. Isolate other setup-mode units.
   Hold the reset-arrows button for about 3 seconds until the screen says `on`;
   that is the user-confirmed BLE-ready indicator. Do not factory reset.
2. Click **Scan**, then explicitly select its BLE name and address. A matching
   service is not identity proof; macOS BLE UUIDs are not label MAC addresses.
3. Click **Scan Wi-Fi networks** and confirm the exact selected BLE target.
   This sends no credentials and starts no firmware transfer.
4. Choose the actual SSID returned by the device, or enter the exact stored SSID.
   Enter the password in the password field. Click **Configure Wi-Fi…** and confirm
   the target and SSID. The field clears when submitted, including failed requests.
   Do not let your browser save the password. Passwords are not stored in browser
   storage, server status, or event logs. The stock device stores the submitted
   credentials; this is not a read-only operation.
5. Stop and check the result. **Credentials accepted** means stock commands
   returned success, not that Wi-Fi works. The device Wi-Fi report is separate;
   a connected-looking status does not establish DHCP or LAN reachability.
   Confirm the intended unit’s association/IP in the router and that it can reach
   the exact **firmware download host:port shown in the dashboard** through
   LAN/firewall policy. Restart the server with the current LAN address after the
   Mac changes networks or IP. Different subnets need routing; same Wi-Fi is not
   proof of reachability. A wrong password,
   isolation, timeout, or ambiguous target is a reason to stop, not to flash.
6. Firmware remains a separate explicit action. Only after independent network
   verification, check that confirmation box, review the image hash and target,
   and confirm **Install ESPHome** or **Restore stock**. Wi-Fi actions never flash.

This experimental stock protocol sends WGS over UART and credentials as WSS then
WSs over JSON. It does not clear registration, reset credentials first, reboot,
or install certificates. Reply IDs are not established: serialized replies and
command acceptance are not an independent association measurement. Passwords are
sent over local HTTP to the local server and BLE to the stock unit, not through
an authenticated application-layer encrypted channel. Keep this local tool private.

## Fresh-device checklist

1. Build a hardware-compatible private device YAML with a distinct hostname and
   friendly name. Configure your own Wi-Fi and API/OTA credentials. Back up
   configuration and secrets privately.
2. Load its application-only image in the dashboard and review bytes/SHA-256.
3. Put only the intended stock unit in BLE-ready mode, scan, explicitly select it,
   and physically confirm its identity. Probe model, hardware and stock version.
4. Scan Wi-Fi and configure the actual SSID/password if needed. Independently
   verify IP/routing access to the displayed download host; BLE acceptance alone
   is not Wi-Fi connectivity proof. Credentials never belong in notes or logs.
5. Review protocol, image hash, OTA slot fit and recovery limits. Confirm install
   separately. Neither loading an image nor configuring Wi-Fi starts a flash.
6. Verify the intended hostname boots, authenticates in Home Assistant and
   responds to controls. A full HTTP transfer or reboot is not sufficient.

Use ESPHome native OTA for later updates, not this stock provisioning tool.
If moving networks, verify new API/OTA access before removing the old path.

## Prepare an image

Build your own hardware-compatible ESPHome configuration separately, using its
required ESPHome version and dependencies. Supply the resulting
`firmware.ota.bin`, not `firmware.factory.bin` or a full-flash backup. No Rest+
configuration is included in this standalone tool; obtaining and reviewing one
is a prerequisite for Install ESPHome, not for Wi-Fi setup.

Never commit secrets or share the compiled binary: it embeds your credentials.
Keep an encrypted backup of the YAML/secrets for future native ESPHome OTA updates.

## Load another device image without restarting

Build each device's application separately, with its own hostname/friendly name.
In the dashboard, choose the resulting `firmware.ota.bin` and click **Load ESPHome
image**. Loading validates the application image and updates its byte count and
SHA-256; it never sends a device command. Factory/merged images are not suitable.
The local endpoint accepts at most 8 MiB and refuses image changes during an
operation. The image stays in memory only; select it again after a server restart.
Do not share these binaries: they contain your Wi-Fi and API secrets.

The server itself does not need to be rebuilt per device. `--esphome` is optional:
start once for Wi-Fi setup, then load each compiled image through the dashboard.
Selecting a file does **not** load it. A pending selection or failed upload blocks
Install ESPHome until a load succeeds. The dashboard clears the picker only after
the server confirms the upload and shows the server-loaded filename, bytes and
SHA-256 in both the image panel and final confirmation. CLI-loaded images show
their source path; uploaded names are labels, not proof of device compatibility.
Always review the image hash, target probe, protocol and final confirmation.

After updating the flasher's Python source, stop and restart the running server,
then reload the page. The HTML is read live but Python code is not: a newly loaded
page can otherwise talk to an old backend with a previously cached image. Firmware
actions are blocked unless the backend advertises the current upload/hash safety
contract. The server rejects stale/missing image SHA-256 confirmations before
starting any device operation.

Offline regression checks (no BLE/device access):

```sh
.venv-flasher/bin/python -m unittest discover -s flasher -p 'test_*.py'
node flasher/test_dashboard.cjs
```

## Run

Replace the address below with this computer's LAN IPv4 address, reachable from
Hatch. Do not use localhost, a VPN address or an isolated VLAN address. The dashboard
shows the actual configured firmware download host and port; check it before each
transfer. If this Mac changes networks or IP address, stop the server and restart
it with the current LAN IPv4 address. Different subnets need working routing and
firewall access. Sharing a Wi-Fi name does not prove the Hatch can reach the Mac.

The dashboard also accepts an optional **operator-supplied target IPv4** and a
configurable comparison prefix length. The default **/24 is an assumption**, not
a discovered netmask. Use the prefix appropriate to your network. With no target
IP the result is unknown. A different-subnet warning is advisory: acknowledge it
(or confirm the warning when flashing) to proceed after checking routing and
firewalls. Same-subnet output is not proof of reachability; the independent
network-verification checkbox and final firmware confirmation remain required.
Changing BLE target clears the target IP, restores /24, and clears acknowledgment.
Changing IP, prefix, or server host/port invalidates the acknowledgment. Target
IP/prefix edits take effect live without restarting the server; changing the
server’s advertised address itself still requires restarting the server.

```sh
.venv-flasher/bin/python flasher/server.py \
  --esphome /path/to/your/build/firmware.ota.bin \
  --advertise 192.168.1.20
```

Open http://127.0.0.1:8000 on the same computer. Grant Bluetooth permission if
asked. Allow inbound TCP 8000 from Hatch through the host/network firewall. Keep
Hatch on stable power. Hold its reset-arrows button for about 3 seconds until the screen says `on`
(BLE ready); do not factory reset. Click Scan, select the correct RIOT Plus device, provision Wi-Fi if needed, then independently verify network access. Check the
verification box, choose Install ESPHome, review the image hash, and confirm. Scan reads candidate GATT services because Hatch may
omit its WSUP service from advertisements. Scanning does not send update commands.

Firmware is available on LAN only through a temporary unguessable URL after an
explicit flash command; controls are localhost-only. Do not port-forward this
server. Stop it with Ctrl-C after use. Python/Bleak OS support and network policies
vary; the tested hardware and stock versions are listed above.

Progress measures bytes written to the HTTP connection, **not flash completion**.
BLE OK means command accepted, not successful update. Confirm independently that
ESPHome appears in HA and authenticates with your key. Use your firmware
configuration's native ESPHome OTA instructions for future updates, not this tool.

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

The offline tests use fake BLE endpoints and synthetic images. They do not
verify a real device's firmware compatibility, Wi-Fi connection, flash or boot.
