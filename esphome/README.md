# ESPHome firmware

- **`hatch-rest*.yaml`** — the working stock replacement for the Hatch Rest 2nd gen (RIoT). See
  [../docs/esphome-plan.md](../docs/esphome-plan.md#replacement-config) for what it does.
- **`hatch-riot-probe.yaml`** — a read-mostly diagnostic firmware for confirming the hardware map. Read
  [../docs/probe-firmware.md](../docs/probe-firmware.md) (safety model and test plan) **before flashing it**.

| File | Purpose |
|---|---|
| `hatch-rest-base.yaml` | **stock replacement** for the Hatch Rest 2nd gen (RIoT): night light, speaker (Home Assistant media + **Music Assistant via Sendspin**), buttons, touch, clock (or custom text), status LEDs, toddler lock, Bluetooth Wi-Fi setup (Improv), optional SD card audio cache, repeat. Shared base; no `external_components` |
| `hatch-rest.yaml` | local build: base + components from `components/` |
| `hatch-rest-builder.yaml` | ESPHome Builder add-on: base + components pulled from GitHub (see [below](#esphome-builder-add-on)) |
| `hatch-riot-probe.yaml` | probe (diagnostic) config |
| `bluetooth-proxy.yaml` | optional add-on: Home Assistant Bluetooth proxy (see [below](#bluetooth)) |
| `riot-plus.yaml` | adds I2C bus 1 (scan only) for RIoT+ boards |
| `components/riot_probe/` | local external component: variant detect, expander polling, guarded writes, register dumps |
| `components/hatch_expander/` | GPIO expander at 0x58 as ESPHome pins (stock AW9110B pin model, bit-banged I2C, verified writes, INT-driven reads) |
| `components/hatch_display/` | clock display over UART1 (stock font, colon, inverted brightness) |
| `components/hatch_status_led/` | Wi-Fi / lock bicolour status LEDs (off / green / red / amber, driven like stock) |
| `components/sd_cache/` | media source that caches web audio on the SD card (see [below](#sd-card-cache)) |
| `components/speaker_source/` | ESPHome 2026.9.1's speaker media player, patched to offer repeat (see [below](#repeat)) |
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
   adopt a unit already running this firmware. From v1.5 the device adds the last three MAC bytes to its name
   (`hatch-rest-a1b2c3`, also used for the fallback access point), so several units can share a network. If a later
   wireless install can't find the device, add a `wifi:` block with `use_address:` set to that name or its IP.
3. **Install → Wirelessly.** Updates keep bootloader rollback.

Customise by adding keys after `packages:` — they override the base (for example `substitutions:`, or a `wifi:` block
with `manual_ip:` for a static address). The file is pinned to release **v1.5**; to update, change the tag in both the
package URL (`…/hatch-rest-base.yaml@<ref>`) and `ref:` (or use `main` to follow every push).

A first flash of a stock unit still has to go over serial (the stock firmware won't accept an ESPHome OTA image):
back up with `../scripts/backup.sh`, then flash `hatch-rest.yaml` or the Builder's "Manual download" image. See
[serial-flashing.md](../docs/serial-flashing.md) for the wiring.

## Buttons

| Control | Action |
|---|---|
| Volume up / down | Volume |
| Brightness up / down | Night light brightness |
| Night light | Night light on / off |
| Touch ring held, then hold night light | Cycle the night light through the colours (white off); release either to stop on a colour |
| Touch ring held, then hold brightness up / down | Make the white light warmer / cooler; release either to stop |
| Touch ring held, then volume up / down | Play the next / previous file in the [SD card cache](#sd-card-cache) |
| Bottom button, short press | Show the Wi-Fi and toddler lock status LEDs for 30 s |
| Bottom button, held | Authorize [Bluetooth Wi-Fi setup](#wi-fi-setup-over-bluetooth-improv) |

The toddler lock disables everything except the bottom button. Touch the ring before pressing the other button:
a button pressed first does its normal action.

The night light has a single white LED, so "warmth" mixes amber from the red and green LEDs into it, from none
(plain white) to full. Cached files are cycled in file-name order, which is a hash of the URL: effectively random,
but the same every time. Nothing plays if the cache is off or empty.

## SD card cache

The **SD Card Cache** switch (off by default) keeps web audio on the Hatch's SD card. With it on:

- A URL played before plays from the card: no download, and it starts in about 0.1 s.
- A new URL streams as usual and is saved to the card while it plays. It's kept only if the whole file downloaded;
  a sound stopped early isn't cached.
- Everything goes in `/esphome_cache` on the card. The stock sound library and other files are never written or
  deleted, so the stock firmware still works if you go back to it.
- When the cache passes 400 MB (`max_size_mb`), the least recently played files are deleted. 50 MB of the card is
  always left free (`min_free_mb`). Files over 100 MB (`max_file_mb`) and endless streams play without being cached.
- Text-to-speech URLs are never cached (`exclude`), since each message is new.
- Home Assistant's `authSig` URL parameter is ignored when matching, because it changes on every request
  (`ignore_query_params`).

With it off, the card is unmounted and powered down. Web audio still goes through `sd_cache`, which then only
streams. **SD Cache Used** / **SD Cache Files** show the size, and **Clear SD Cache** empties it.

**Slow downloads.** This unit's Wi-Fi delivered 30–300 KB/s to the ESP32 (LAN or internet, with a -49 dBm signal),
and a 44.1 kHz stereo WAV needs 176 KB/s. When the download is slower than the audio, `sd_cache` waits before
playing until enough is buffered for the rest of the file to arrive in time (up to 2 MB, in PSRAM). It measures the
speed for 3 s and plans with 85% of it. If the download falls behind anyway, it pauses once to rebuffer instead of
stuttering. Compressed files (MP3, FLAC) rarely need it, and cached plays never do.

For caching to work, the media pipeline asks Home Assistant for **original media URLs** (`format: NONE`) instead of
its FLAC transcoding proxy, whose URL is new on every play. The device decodes MP3, FLAC and WAV itself. Other
formats (AAC, Ogg Vorbis) no longer play through Home Assistant media; Music Assistant (Sendspin) is unaffected.
There is no `resampler` stage: while Wi-Fi was busy it couldn't keep up, and audio slowed by 25–35%. Instead
`sd_cache` sends mono audio as stereo, because the I2S speaker plays 16-bit mono at 44.1 kHz at half speed.
`audio_http` stays as the fallback source and doesn't do this, but it only plays when `sd_cache` is busy.

The stock card had 574 MB free, mostly taken by a second copy of the stock sound library under
`/Downloaded/sound machine backup/` (see [hardware-map.md](../docs/hardware-map.md#card-contents)).

## Repeat

The **Speaker** media player offers repeat (one / off) in Home Assistant, from the media player card or
`media_player.repeat_set`. "All" isn't supported: Home Assistant's ESPHome integration only sends one and off. Repeat
resets to off when the device restarts.

Web audio (which goes through `sd_cache`) loops without a gap: `sd_cache` starts the file again inside its playback
task, so the speaker keeps running. The decoder runs about half a second ahead of the speaker, which covers the
restart. WAV and FLAC loop seamlessly. MP3 may keep a short gap (about 25–50 ms) from the encoder's padding. A loop
that has to download the file again may pause briefly: the first one after a new download if the file isn't on the
card yet, and every one with the cache off. Audio from the `audio_http` fallback repeats by playing again, with a
short gap.

It needs `components/speaker_source/`, a copy of ESPHome's player: the stock one has repeat but doesn't offer it to
Home Assistant, and can't loop a file without stopping the speaker. Re-copy it from the ESPHome release when
upgrading ESPHome, and keep the changes marked `Hatch:`.

## Bluetooth

### Wi-Fi setup over Bluetooth (Improv)

Built into the base config. When the Hatch can't join Wi-Fi (new network, changed password), it advertises over
Bluetooth, and the Home Assistant companion app or [improv-wifi.com](https://www.improv-wifi.com/) can send it new
credentials. **Hold the bottom CONNECT button** to authorize, like the stock pairing flow; authorization lasts one
minute. It's idle while Wi-Fi is connected. The fallback access point (named after the device, e.g.
`hatch-rest-a1b2c3`) still works as before.

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

> **Known issue: Music Assistant (Sendspin) audio stutters with the proxy on.** The ESP32 has one 2.4 GHz radio
> shared by Wi-Fi and Bluetooth, and BLE scanning takes airtime from Wi-Fi. Tested on hardware (2026-10-14, ESPHome
> 2026.9.1, default scan settings, with the [SD card cache](#sd-card-cache)):
>
> | Source | With the proxy on |
> |---|---|
> | Cached sound (played from the SD card) | plays fine |
> | Home Assistant media (HTTP, through `sd_cache`) | plays fine; `sd_cache` buffers ahead |
> | Music Assistant via Sendspin | **stutters** |
>
> Before the SD cache was added, streamed audio stuttered. So the proxy is fine if the Hatch plays Home Assistant
> media (white noise, sounds), but not if you use it as a Music Assistant speaker. That's why it's off by default.

Scan settings are left at ESPHome's defaults on purpose. Don't copy the 1100 ms
`interval`/`window` from older proxy guides: long scan windows can drop Wi-Fi on some access points.
