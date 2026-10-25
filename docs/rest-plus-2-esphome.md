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
