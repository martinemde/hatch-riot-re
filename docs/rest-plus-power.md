# Experimental Rest+ 2 power controller

This is an extraction of `hatch_plus_power` and the read-only
`hatch_plus_indicator_probe` from the Rest+ integration work in #2. It is
**AS IS, without warranty or safety guarantees**. Compilation and host tests
are not battery, charging, thermal, shutdown or wake validation. Do not flash
the compile harness onto a device.

## Scope and dependencies

This change targets upstream `main` and does not require #5 (playback), #6
(SD library), #7 (accelerometer), gestures, presets, or the indicator light
platform. Existing upstream `hatch_display` overlay support is used for power
feedback. The compile fixture uses ESPHome's built-in speaker media player,
not the fork's `speaker_source` or `sd_cache`.

The only expander additions are error-visible load-output initialization,
readback and writes, plus their private multi-byte bit-bang write helper.
The private helper overlaps #7's shared-bus prerequisite: when merging both,
keep one implementation of `bb_write_regs_`. This PR intentionally does not
copy #7's public shared-device transaction APIs or accelerometer component.
Power peripherals use ESPHome's separate native I2C bus instead.

## Hardware boundary

Recovered Rest+ 2 / RIoT Plus hardware8 wiring uses two distinct buses:

| Owner | SDA | SCL | Purpose |
| --- | --- | --- | --- |
| `hatch_expander` | GPIO0 | GPIO22 | AW9110B load/button expander, 0x58 |
| ESPHome `i2c` | GPIO13 | GPIO23 | GreenPAK and wireless receiver |

The load adapter maps logical LED/amplifier pins0/2 to physical expander
pins8/10. Initialization configures only those two outputs low; transactions
preserve unrelated latch/configuration bits. Do not attach a second I2C driver
to either owner's pins. Do not apply this mapping to a similar board without
checking the exact hardware. Battery ADC uses ESP32 ADC1 channel3 (GPIO39).
The fixture records recovered wiring, not an electrical certification.

## Diagnostics first

`hatch_plus_power` requires `hatch_expander_id` and native `i2c_id`.
`allow_control` defaults false. In that mode only RAM0x74..0x76 is read at
0x10; no discovery/reset/programming or load writes occur. Optional raw
`status_74`, `status_75`, `status_76` sensors and voltage flags are available.
Wired/wireless voltage present is not measured charging current.
`charge_enable_logic` is a logic flag, not proof that charging works.

For separate bring-up logs, `hatch_plus_indicator_probe` needs only `i2c_id`.
It makes ACK-only discovery transactions at0x10, falling back to0x08 only on
NACK. It reads RAM0x32..0x35 and0x74..0x76 only after0x10 ACK, never resets
or writes register payloads, and exposes no Home Assistant entities. ACK is
not device identity; raw indicator fields do not establish physical colors.
Both components share the configured native bus in main-loop context.

## Opt-in control

`allow_control: true` requires `light_id`, `media_player_id`, `speaker_id`,
`display_id` and exactly four `pwm_channels`. This enables the recovered power
scheduler, ADC/battery/thermal calculations, charging policy, source handling,
load sequencing and power-button shutdown. No separate acknowledgment option
exists. `allow_nvm_programming` is independently false by default and requires
control; enabling it can erase/program persistent GreenPAK configuration.
Establish recovery and verify exact hardware before considering that opt-in.
The compile harness leaves it disabled.

Control-mode battery percent, report age/acceptance and owner-fault entities are
optional. An accepted report is not evidence of electrical validity. Owner
fault is not a complete protection check; startup errors may retain recovered
stock behavior rather than stop the controller. No thresholds or timing
constants were changed during extraction.

Shutdown displays OFF above the clock/source overlay, stops playback/light,
and best-effort blanks the three GreenPAK indicator fields. Blanking failure
cannot block battery protection. Wi-Fi is disabled only after a successful
final power-off transaction and restored if the controller cancels while still
powered. I2C success is not proof of physical rail removal. Indicator helper
APIs in the power owner do not require the separate indicator light platform.

## Verification and bring-up limits

Use ESPHome2026.10.0 for the pinned verification gate:

```sh
python -m pytest tests/test_plus_power_controller.py tests/test_plus_register_transport.py tests/test_plus_native_i2c_transport.py tests/test_plus_pwm_output_state.py tests/test_plus_runtime_chain.py tests/test_plus_runtime_owner.py tests/test_hatch_plus_indicator_probe.py esphome/components/hatch_plus_power/tests/ -q
python -m esphome compile esphome/hatch-plus-power-compile.yaml
```

The self-contained harness deliberately enables control to compile its full
integration with ordinary light, display, speaker and media player objects.
Its placeholder Wi-Fi and unbounded PWM defaults are **not deployment values**.
CI compiles it but does not publish a flashable artifact.

Before real-hardware testing, establish full backup/recovery, inspect exact
signal directions and pin maps, verify supply/charge-current and battery/thermal
measurements independently, and supervise power transitions. Separately check
battery-only off, power-button wake, external-power wake, boot failures,
OTA/reboot/safe-mode and retained outputs. Host tests exercise recovered logic
and injected bus failures, not those electrical outcomes. Stock audio/UI parity
and hardware safety remain unproven; this extraction does not change that.
