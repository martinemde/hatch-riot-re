# Rest+ 2 bottom indicator lights

The `hatch_plus_indicator` light platform controls the Wi-Fi, battery and toddler
lock windows through the existing `hatch_plus_power` owner. It never claims GPIO
pins or creates a second I2C bus. Do not use non-Plus `hatch_status_led` on a Plus.

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [hatch_plus, hatch_plus_power, hatch_plus_indicator]

light:
  - platform: hatch_plus_indicator
    name: Wi-Fi Indicator
    indicator: wifi
    hatch_plus_power_id: plus_power_diagnostics
  - platform: hatch_plus_indicator
    name: Battery Indicator
    indicator: battery
    hatch_plus_power_id: plus_power_diagnostics
  - platform: hatch_plus_indicator
    name: Toddler Lock Indicator
    indicator: lock
    hatch_plus_power_id: plus_power_diagnostics
    hatch_plus_id: hatch_controller
```

These are red/green bicolour LEDs, not full RGB lights. They support off, red,
green and yellow (both colours). The RGB UI maps nonzero red/green channels to
those discrete states; blue alone has no output. There is no hardware dimming.
Select effect **Automatic** to follow device state; select **None** for manual
colour control. In Automatic, on/off means show/hide the status; firmware colour
and charging blink are hardware-only and do not publish light-state changes.
Turning off hides status; a plain turn-on resumes Automatic using ESPHome's
`resume_effect: true` default for this platform. Manual mode stays manual across
an off/on cycle. Automatic is the boot default.

Defaults:
- Wi-Fi disconnected: red; connected to the access point: green. HA/API/cloud
  connectivity does not change this state.
- Battery: below 25% red; 25% to below 50% yellow; 50% through 100% green.
  Before the power owner has a battery report, the indicator is off.
- Charging: the battery colour alternates 500ms on / 500ms off. This means a
  fresh successful power telemetry sample has charge-enable logic AND wired or
  wireless voltage present. It is not a measurement of charging current.
  A failed telemetry read disables blinking until a successful read.
  Home Assistant sees a steady enabled state, not individual blink phases.
- Toddler lock enabled: red; disabled: off.

There is no tilt gate: indicators work upright too. No changes to the nightlight,
clock brightness, presets or power-protection thresholds are involved.

Writes require the existing power owner to have `allow_control: true`, a
programmed device discovered at address 0x10, and an active, non-shutdown owner.
This platform never enables power control or NVM programming itself. Diagnostic
configurations (`allow_control: false`) do not drive indicators. Do not enable
experimental power control solely to test an LED without reviewing power safety.

Stock 7.1.645 evidence: indicator writer 0x40139f14 (Plus branch 0x40139f98),
RAM-bit helper 0x40117fac. Six-bit fields are Wi-Fi 0x192, battery 0x198, lock
0x1a4. Codes: off 0x11, green 0x00, red 0x17, yellow 0x10. Writes read/modify/write
only their field, including the lock field spanning two bytes. Existing original-
instruction tests cover the generic RMW helper; physical colours still need
confirmation on the Plus hardware. No flashing is implied by source integration.

Validation (2026-11-03): full ESPHome 2026.10.0 compile of the local
`homeassistant/esphome/hatch-plus.yaml` passed, including all three lights.
Focused indicator, owner, register-transport, schema and metadata suite:
45 passed, 1 skipped (opt-in stock QEMU execution). No OTA upload performed.
