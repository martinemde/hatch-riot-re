# Rest+ 2 ESPHome example

`esphome/hatch-plus-example.yaml` is a private-build development example, not a
hardware-safety-approved image. Keep its experimental warnings and disabled power
control. This update does not establish charging, thermal, shutdown or recovery
safety. Use your own ignored `esphome/secrets.yaml` for `wifi_ssid`,
`wifi_password`, and a unique ESPHome `encryption_key`. Never publish rendered
configuration, flash backups, or device certificates.

## Display

Clock Display is an on/off/brightness light. Show Clock chooses clock or custom
Display Text. Custom text scrolls repeatedly, with a two-second first/final pause
and 500 ms steps. The native API action `send_text` takes `message` and temporarily
scrolls it once, then restores the latest clock/text without changing settings.
Four glyphs or fewer show for two seconds total. In Home Assistant, invoke the
actual ESPHome action ending `_send_text` with `message: "HELLO"`.

## Buttons and blueprint

Each of the seven buttons emits `esphome.hatch_button` with `device`, `button`,
and `gesture` (`tap`, `hold`, `double`, `triple`). The multi-click window defaults
to 350 ms after release. Single/double actions wait for the window; triple fires
on the third release. Hold is 800 ms; volume/brightness holds repeat every 150 ms.
Double/triple do not execute the native single-tap action. Native tap/hold actions
remain active; Home Assistant actions are additional.

Import `blueprints/automation/hatch-rest/touch-ring-favorites.yaml` from this fork.
Select **Firmware button events / custom actions**, match the event `device`
value to the sample's `${name}` substitution, and assign any of its 28 action
sequences. Do not also run legacy raw-sensor mode for the same controls. Allow
the device to perform Home Assistant actions in its ESPHome integration settings.
No automation is created or enabled by the sample.

## Audio

Selecting a Stock Sound turns Loop Sound on and plays that sound repeatedly.
You may turn Loop Sound off afterward. Other/custom URLs retain the existing
Loop Sound behavior. The player also retains repeat mode when switching from
Sendspin/internal-playlist sources to local files. Stock sounds use `hatch-library:///Rest_Sounds/wav/...`
paths; the corresponding stock sound files must already be available on the SD
card. The example does not distribute those files or use Hatch account credentials.

## Entity metadata

Battery uses battery/%/measurement; power-presence flags use power. Raw codes
have no measurement state class. Charge Enable Logic is an electrical level,
not proof of charging current. The wireless icon is
`mdi:battery-charging-wireless`; it does not assert actual charging. No heat
class is inferred from raw status flags. Cache size uses data_size/MB/measurement.

The separately developed power-controller and orientation/probe experiments are
not part of this update. The sample keeps the published power schema and opt-out
control behavior instead of depending on unpublished local development files.
