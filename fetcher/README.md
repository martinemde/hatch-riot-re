# Hatch firmware fetcher

Small scripts that use Hatch's cloud API with **your own account** to find and download the firmware image Hatch
serves for your device(s). This is how `firmware/riot_e831cd79dc02_riot_DFU_7_1_644.bin` was obtained.

- `hatch_auth.py`: log in, get and cache an API token.
- `hatch_firmware.py`: list devices, call the firmware version-check endpoint, and download any image it returns.
- `hatch_api.py`: shared client (endpoints, auth header, request shapes).

Nothing here talks to the device or flashes anything; it only fetches the file over HTTPS.

## How it was derived

Endpoints and request shapes were recovered by decompiling the Hatch Sleep Android app (`com.hatchbaby.rest`
6.40.1) and cross-checked against the community library <https://github.com/dahlb/hatch_rest_api>.

| Purpose | Request |
|---|---|
| Login | `POST https://data.hatchbaby.com/public/v1/login` `{email,password}` → `{token}` |
| Auth | header `X-HatchBaby-Auth: <token>` |
| List devices | `GET /service/app/iotDevice/v2/fetch?iotProducts=restMini,restPlus,riot,...` |
| Firmware check | `GET /service/app/firmwareVersion/v1/check?os=Android&product=<p>&macAddress=<mac>&currentAppVersion=&currentFirmwareVersion=&currentHardwareVersion=&mrb=false` |

The firmware-check response maps to the app's `FirmwareVersionApi(url, <bool>, <bool>, version)`; `url` is the
firmware download link (on Hatch's CDN, typically short-lived, so download promptly). The app flashes it over BLE
with Nordic DFU.

## Usage

Needs Python 3.10+ and `requests` (in the repo's `requirements.txt`). Run from the repo root:

```sh
# 1. Authenticate (token cached at ~/.cache/hatch/token.json)
export HATCH_EMAIL=you@example.com
export HATCH_PASSWORD='your-password'
.venv/bin/python fetcher/hatch_auth.py

# 2. See what's on the account
.venv/bin/python fetcher/hatch_firmware.py --list

# 3. Check and download firmware for every device, into firmware/
.venv/bin/python fetcher/hatch_firmware.py --insecure
```

`--insecure` skips TLS verification for the download: `firmware.hatchbaby.com`'s certificate is expired. Other
options:

```sh
fetcher/hatch_firmware.py --mac AA:BB:CC:DD:EE:FF        # one device
fetcher/hatch_firmware.py --product riot --mac ...       # force the product type
fetcher/hatch_firmware.py --check-only                   # show the JSON, don't download
fetcher/hatch_firmware.py --current-fw 1.2.3             # "is there anything newer than 1.2.3?"
fetcher/hatch_firmware.py --out ./fw                     # another download directory
```

By default the scripts send **empty** current firmware/hardware versions, so the server reports the latest
available image. Pass `--current-fw` / `--current-hw` to ask whether something newer than a given version exists.

Don't run these with `python -I` like the analysis tools: they import `hatch_api` from their own directory.

## Config / env vars

| Var | Meaning | Default |
|---|---|---|
| `HATCH_EMAIL`, `HATCH_PASSWORD` | credentials for `hatch_auth.py` | prompt |
| `HATCH_TOKEN` | pre-obtained token for `hatch_firmware.py` | cached token |
| `HATCH_BASE_URL` | API base | `https://data.hatchbaby.com/` |
| `HATCH_TOKEN_CACHE` | token cache path | `~/.cache/hatch/token.json` |
| `HATCH_USER_AGENT` | User-Agent header | `hatch_rest_api` |

## Caveats

- Hatch can change the API at any time. If the firmware check returns an unexpected shape, `hatch_firmware.py`
  prints the raw JSON, and `_find_url()` scans the whole response for a firmware-looking URL rather than assuming
  one key.
- `mrb` is a boolean flag the app passes (`DeviceService.checkFirmwareVersion`); its meaning isn't documented. It
  defaults to `false`.
- The token is account-scoped: treat `~/.cache/hatch/token.json` like a password. Only your own account and
  devices are accessible. Downloaded images are vendor property and are git-ignored (`firmware/`, `*.bin`).
