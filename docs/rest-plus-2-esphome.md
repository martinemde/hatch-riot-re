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
and `gesture` (`tap`, `hold`, `double`, `triple`). Buttons whose native `tap`
action is `volume_up`, `volume_down`, `brightness_up`, or `brightness_down` apply
one step and emit `tap` immediately on each short release, including rapid taps.
These buttons do not emit `double` or `triple`.

Other buttons use the multi-click window, which defaults to 350 ms after release.
Single/double actions wait for the window; triple fires on the third release.
Double/triple do not execute the native single-tap action. Hold is 800 ms;
volume/brightness holds with no explicit hold action repeat every 150 ms, with
no extra step on release. Explicit hold actions remain release-only; scene
management keeps its separate thresholds. Native tap/hold actions remain active;
Home Assistant actions are additional.

Import `blueprints/automation/hatch-rest-plus-2/buttons.yaml` from this fork
(**Hatch Rest+ 2 buttons**). Match the event device name to the sample's `${name}`
substitution and assign any of its 28 action sequences. This blueprint uses only
firmware events; it has no legacy sensor/favorites inputs or mode selector.
Allow the device to perform Home Assistant actions in its ESPHome integration
settings. No automation is created or enabled by the sample.

The separate `hatch-rest/touch-ring-favorites.yaml` blueprint keeps the original
Rest touch-sensor/favorites behavior. If migrating an existing combined-blueprint
event automation, recreate it with the Rest+ 2 blueprint and copy its device,
lock and custom action settings before removing the old automation. Do not run
both automations for the same controls.

## Named presets

The native API actions `store_preset(index, name)` and `apply_preset(index)` use
slots **0–9** (`FAV0`–`FAV9`). In Home Assistant they appear under ESPHome with
the device name prefix, for example `esphome.hatch_plus_store_preset`.

Set the device how you want it, then call `store_preset` with the slot and name.
This saves the night light's on/off, color and brightness, speaker volume,
selected Stock Sound and Loop Sound setting, Show Clock setting, Clock Display
on/off and brightness, and Display Text. It does not capture arbitrary streaming
URLs. An empty name uses the slot label. Successful saves show `SAVn` once.
Applying restores those settings, briefly shows the saved name (or `FAVn`), then
returns to the saved clock or text. Presets persist across reboot; reboot does
not apply one or start playback.

For example, in Home Assistant Developer Tools → Actions:

```yaml
action: esphome.hatch_plus_store_preset
data:
  index: 1
  name: Bedtime
```

```yaml
action: esphome.hatch_plus_apply_preset
data:
  index: 1
```

Names allow up to 32 UTF-8 bytes; Display Text allows up to 128 bytes.
Preset storage and recall run on the device without Home Assistant or Wi-Fi;
the clock still needs a valid time from the existing time source. Stock audio
must already exist on the SD card.

Existing favorite next/previous/play operations share these slots and restore
a named preset when they encounter one; capturing an existing named slot
refreshes its full snapshot and keeps its name. Ordinary long-press timing
remains 800 ms; scene management uses the separate 5/10-second thresholds below.
Toddler Lock is retained.

## On-device scene buttons

Button mappings are configurable in `hatch_plus.buttons`; there is no fixed
requirement to use the top or night-light button. The example uses:

```yaml
buttons:
  - name: touch
    tap: next
    hold: stop
  - name: night_light
    tap: toggle_light
    hold: manage_preset
```

Set the top touch button's `tap` to `none` or another action to disable preset
cycling. Put `hold: manage_preset` on the button you want to use for storage,
or omit it to use only the API. It requires the `presets` entity references.
Only one button may use `hold: manage_preset`, so a delete preview always
identifies the one pending deletion.

A short top tap cycles through occupied slots, skipping deleted slots. On the
configured save/delete button, release after **5 seconds but before 10 seconds**
to capture the current settings into the first empty slot (`FAV0`–`FAV9`).
The display shows `SAVn` for two seconds after release. Holding for **10 seconds**
shows `DELn` while still held; releasing deletes that slot, never its name.
The delete target is the current slot when the 10-second threshold is reached.
A full store shows `FULL` rather than overwriting a scene. No selected scene
shows `NONE` rather than deleting something else. Toddler Lock suppresses these
button operations under the existing lock policy.

## Audio

Selecting a Stock Sound first disconnects the current Sendspin session, including
paused sessions, so it releases the audio pipeline. This also applies when
reselecting the same sound or recalling a local preset. Choosing None disconnects
Sendspin and stops playback. Sendspin remains enabled and discoverable; remote
playback can reconnect later. A replacement URI discards older queued playback
requests for that pipeline rather than waiting behind a stale request.

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

## Experimental power and peripheral components

The sample uses the shared GPIO13/23 I²C bus for GreenPAK/receiver access;
the load expander remains on GPIO0/22. The power schema requires
`i2c_id` and `acknowledge_experimental_power_risks: true`. This replaces the old
`acknowledge_not_power_management` option. The main example keeps `allow_control`
and `allow_nvm_programming` false. The dedicated power-development configuration
explicitly enables experimental control but keeps NVM programming disabled.
Do not flash that configuration without reviewing its warnings.

Optional battery-report age/acceptance and owner-fault diagnostics are available.
Acceptance is not proof of electrical validity; the fault indicator is not a
complete battery-protection check. Battery and thermal histories use corrected
rolling sums; thresholds are unchanged. GreenPAK programming needs separate
explicit permission and recovery planning.

`hatch_accelerometer` exposes optional DA213 axes, tilt and orientation. Its
`allow_initialization` defaults false (read-only discovery). The
`hatch_plus_indicator_probe` component performs read-only register logging and
creates no Home Assistant entities. Optional components are not enabled by the
main example automatically.

Host tests and compilation are not hardware-safety approval. Reboot/OTA/safe-mode,
off/wake behavior, retained outputs after startup errors, actual charge current,
thermal calibration and independent protection remain incompletely validated.
