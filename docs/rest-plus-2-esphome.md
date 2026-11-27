# ESPHome on Hatch Rest+ 2nd gen

Use this package only for **HBRESTPLUS2** (RIoT+). For HBREST2 (RIoT), use
[the Rest configuration](../esphome/hatch-rest.yaml).

## ESPHome Builder setup

1. In ESPHome Builder, add `wifi_ssid`, `wifi_password`, `fallback_ap_password`,
   and `api_encryption_key` to **Secrets**. For a unit already running this
   firmware, keep its existing node name and encryption key.
2. Create a device and replace its YAML with
   [hatch-plus-builder.yaml](../esphome/hatch-plus-builder.yaml). Set the name
   and friendly name. The starter follows `main`.
3. Use **Validate**. The starter takes the package (board wiring, audio sources,
   button behavior) and its components from `main`. Do not substitute Rest firmware for a
   Rest+ package.

For a local build, use ESPHome 2026.10.0 and
[hatch-plus-example.yaml](../esphome/hatch-plus-example.yaml). Copy
`esphome/secrets.yaml.example` to `esphome/secrets.yaml` and fill in your local
values. Run `esphome compile esphome/hatch-plus-example.yaml` from the repository
root. Never flash an example compiled with disposable credentials.

## Battery and power management

The Rest+ package automatically enables its power controller. It manages
charging, battery reporting, low-battery shutdown, and the power rails for
the night light and amplifier. GreenPAK nonvolatile memory programming stays
disabled. This does not change the standard Rest configuration.

If you installed an earlier preview, use the current Builder starter and remove
the old power substitution from your device YAML. Keep your device name and secrets.

## Home Assistant controls

Open your Hatch under **Settings** > **Devices & services** > **ESPHome**.
The package creates the following entities. You do not need to add them to
ESPHome YAML. If you disabled an entity in Home Assistant, open its settings
and turn on **Enabled**.

| Entity | How to use it |
| --- | --- |
| Night Light | Set the RGB color, white level, or brightness. |
| Speaker | Play audio or set the volume. Select stock sounds separately in **Stock Sound**. |
| Clock Display | Set display brightness or turn the display off. |
| Show Clock | Turn on to show the time from Home Assistant. Turn off to show **Display Text** instead. |
| Display Text | Enter scrolling text to show while **Show Clock** is off. |
| Toddler Lock | Turn on to suppress physical button gestures and their local actions. Home Assistant can still control the device. |
| Stock Sound | Select a sound from the stock SD card, or select **None** to stop playback. |
| Loop Sound | Turn off to stop the selected sound after one play. Selecting a stock sound turns looping back on. |
| SD Card Cache | Turn on to save web audio to the SD card as it plays and replay it from the card. Off by default. |
| SD Cache Used, SD Cache Files | Read how much of the card the cache uses. |
| Clear SD Cache | Delete every cached file. Stock sounds are not affected. |
| Current Scene | Read the scene slot loaded or saved last (`1` to `9`), or `None`. |
| Battery | Read the reported battery percentage. |
| Power Source | Read `base`, `plug`, or `battery`. The base takes priority when both inputs are present. If the power status cannot be read, it reports `unknown`. |
| External Power Present | Check whether the wired power input is detected. |
| Wireless Voltage Present | Check whether voltage from the wireless charging base is detected. |
| Tilt Angle | Read the angle in degrees. The package reports changes of at least 2°. |
| Upright | Trigger an automation when the Hatch becomes upright or leaves the upright position. |
| Wi-Fi Signal, Uptime | Read the connection strength and the time since the last restart. |
| Automatic Indicators | Turn off to stop the firmware from driving the bottom indicator windows. See [Status indicators](#status-indicators). |
| Wi-Fi Indicator, Battery Indicator, Toddler Lock Indicator | Read or set the colour of each bottom window: `Off`, `Red`, `Yellow`, or `Green`. |
| Restart | Restart the device. |

The ESPHome integration also exposes device actions in **Developer tools** >
**Actions**. `save_scene`, `load_scene`, and `delete_scene` take a `slot`
from `1` to `9`. `next_scene` and `previous_scene` step through the saved
slots. A scene saves the night light, the sound that is playing, Loop Sound, and
the volume. The clock confirms each action, for example `SAV1` or `FAV1`. See
[Scenes](hatch-scenes.md) for details. The integration also exposes `send_text`
for one-time scrolling text.

### Add raw acceleration or Not Upright

**Tilt Angle** and **Upright** are on by default. To expose raw acceleration
in g or a separate **Not Upright** binary sensor, add this block at the top
level of your device's ESPHome YAML, next to `packages`. Keep only the entries
you want. ESPHome merges the block with the package's `hatch_accelerometer`
configuration. Validate and rebuild to add the entities.

```yaml
hatch_accelerometer:
  acceleration_x:
    name: Acceleration X
  acceleration_y:
    name: Acceleration Y
  acceleration_z:
    name: Acceleration Z
  not_upright:
    name: Not Upright
```

If you only need to trigger an automation when the Hatch tips, use the
existing **Upright** entity with a state trigger to `"off"`.

### Button gestures

Each physical control has a Home Assistant event entity. The event entity's
`event_type` attribute identifies the gesture.

| Event entity | Event types | Default local action |
| --- | --- | --- |
| Touch Ring | `tap`, `hold`, `double`, `triple` | Tap loads the next scene. Hold to turn off the light and speaker, 5 seconds to save a new scene, or 10 seconds to delete the current scene. Holds act on release; the clock shows `SAV?` or `DEL?` first, even when it is turned off. |
| Bottom Button | `tap`, `hold`, `double`, `triple` | None. Power shutdown is separate. |
| Night Light Button | `tap`, `hold`, `double`, `triple` | Tap toggles the light. |
| Volume Up Button, Volume Down Button | `tap`, `hold` | Tap steps the volume once. Hold repeats until release. |
| Brightness Up Button, Brightness Down Button | `tap`, `hold` | Tap steps the brightness once. Hold repeats until release. |

Hold the Touch Ring first, then press a front button, to change what that
button does. Neither control sends an event for the combination, and the ring
does not stop playback, save, or delete a scene when you let go.

| With the Touch Ring held | Local action |
| --- | --- |
| Night Light Button (hold) | Cycle the night light through the colours. |
| Brightness Up / Down Button (hold) | Make the white light warmer / cooler. |
| Volume Up / Down Button | Play the next / previous file in the SD card cache. |

Double and triple taps have no built-in action. If your automation listens for
`esphome.hatch_button`, replace that trigger with a state trigger on the new
event entity. No permission for the device to send Home Assistant events is
required.

### Let Home Assistant handle button actions

Local button actions are on by default and work without a Home Assistant
connection. To run only your Home Assistant automations on a button gesture,
add `enable_local_button_actions: "false"` to the existing `substitutions`
block in your device's ESPHome YAML. Do not add a second `substitutions` block.

```yaml
substitutions:
  enable_local_button_actions: "false"
```

Validate and rebuild. All seven event entities remain available, but the
buttons no longer adjust the light or volume, stop playback, or load, save,
or delete scenes on their own. You can still use the entities and scene
actions from Home Assistant. **Toddler Lock** still suppresses gesture events.
The setting does not change power-button shutdown or battery management.

### Automate a button gesture

In the Home Assistant automation editor, create an automation and choose
**Edit in YAML**. This example turns on a light when you double-tap the touch
ring. Replace `event.example_touch_ring` and `light.example_lamp` with your
entity IDs. Use the event types in the table above to match other gestures.

```yaml
alias: Turn on a light on Hatch double tap
triggers:
  - trigger: state
    entity_id: event.example_touch_ring
    not_to: ["unknown", "unavailable"]
conditions:
  - condition: template
    value_template: >-
      {{ trigger.from_state is not none
         and trigger.to_state is not none
         and trigger.from_state.state != 'unavailable'
         and trigger.to_state.attributes.get('event_type') == 'double' }}
actions:
  - action: light.turn_on
    target:
      entity_id: light.example_lamp
```

The trigger watches the event entity's timestamp, not changes to its
attributes. The condition skips initial entity registration and transitions
from `unavailable`.

### Status indicators

The three small windows on the bottom of the Rest+ show Wi-Fi, battery, and
toddler lock status. Each window is a red/green LED. It shows only red,
green, or yellow (both on), and has no brightness control. For that reason,
each window is a select entity with the options `Off`, `Red`, `Yellow`, and
`Green`, not a light.

**Automatic Indicators** is on by default and follows the stock firmware.
The windows light only while the Hatch is not upright: tipped more than about
60°, on its side, or upside down. While the Hatch is upright, or the
accelerometer has no valid reading, all three windows are off.

| Window | Red | Yellow | Green | Off |
| --- | --- | --- | --- | --- |
| Wi-Fi | No Wi-Fi connection | Wi-Fi, but no Home Assistant connection | Connected to Home Assistant | — |
| Battery | 10% or less | 11% to 20% | Above 20% | No battery reading yet |
| Toddler Lock | Toddler Lock is on | — | — | Toddler Lock is off |

Stock firmware turns the Wi-Fi window green when the Hatch cloud is
connected. This package uses the Home Assistant connection instead. On the
Rest package, the lock window is green; on the Rest+, stock uses red.

To take over the windows from Home Assistant, select a colour on any
indicator entity. This turns **Automatic Indicators** off and turns the other
two windows off. The windows then keep the colours you set, also while
upright. To stop the automatic behaviour and keep the windows dark, turn
**Automatic Indicators** off. Turn it back on to return to stock behaviour.
The setting is kept across restarts. The windows are off at boot until the
power controller is ready, and the controller turns them off at shutdown.

## Installation and recovery

This example does not provide a first-install or return-to-stock path. Identify
the exact model and device MAC before any firmware write. Make and verify a
full-flash backup if you have serial access. Do not follow the Rest serial wiring
or flash instructions for a Rest+. The [local Rest+ flasher](../flasher/README.md)
covers supported stock firmware versions. An application-only install does not
create a backup.

If Wi-Fi is unreachable, connect to the password-protected fallback hotspot and
open `http://192.168.4.1` to enter Wi-Fi credentials. Access to the hotspot also
grants access to the captive portal's browser OTA endpoint. Use encrypted native
ESPHome OTA for regular updates.

While Wi-Fi is unreachable, the device also offers Bluetooth Wi-Fi setup
(Improv). Hold the bottom button to authorize it for a minute, then send new
credentials from the Home Assistant app or improv-wifi.com. To remove it, add
this to your device's ESPHome YAML:

```yaml
improv_ble: !remove
esp32_ble: !remove
```

## Package layout

- [hatch-plus-base.yaml](../esphome/hatch-plus-base.yaml) holds the fixed wiring,
  buses, and audio pipeline.
- [hatch-plus-controls.yaml](../esphome/hatch-plus-controls.yaml) connects each
  physical control to its local action and native event entity.
- [hatch-plus-scenes.yaml](../esphome/hatch-plus-scenes.yaml) wires the local
  scenes, stock sounds, and clock settings into the same package.
- [hatch-plus-indicators.yaml](../esphome/hatch-plus-indicators.yaml) drives
  the bottom indicator windows.

The device YAML provides the components. The local example builds them from
this checkout. The Builder starter takes the package and the components from
`main` of this repository. To stay on a release, change both refs to its tag.
