# Optional Hatch accelerometer

On a Hatch fitted with the [MiraMEMS DA213](hardware-map.md#accelerometer-da213),
use the existing `hatch_expander` on stock **I2C0**: GPIO0 (SDA) and GPIO22
(SCL). The expander is at `0x58`; the DA213 is at 7-bit address `0x27` and
returns `0x13` in register `0x01`. A tested Rest+ 2 (hardware `8.0.1`,
stock `7.1.503`) instead responds at `0x62` and uses the MSA311 profile.
On Rest+, the native I2C1 bus on GPIO13/23 is for power peripherals, not the
accelerometer. Do not create a second I2C master on GPIO0/22.

## Quick start

Add `hatch_accelerometer` alongside `hatch_expander` in your existing
`external_components` list. Add this block and replace `io_exp` with the ID of
your GPIO0/22 expander if it differs:

```yaml
hatch_accelerometer:
  hatch_expander_id: io_exp
  tilt_angle:
    name: Tilt Angle
    filters:
      - delta: 2.0
```

This creates only Tilt Angle. The `delta` filter reduces updates from the
reported ±1° jitter. Configured entities are enabled by default in Home
Assistant. The component does not create entities you omit. To create an entity
but leave it disabled by default in Home Assistant, set
`disabled_by_default: true` on that entity.

## Upright automation

With `logger:` enabled, add `upright` under your existing
`hatch_accelerometer` block, at the same level as `tilt_angle`. The automation
logs when the Hatch returns upright:

```yaml
  upright:
    name: Upright
    on_press:
      then:
        - logger.log: "Hatch is upright"
```

ESPHome runs `on_press` when Upright changes from off to on. Upright follows
the stock Z-axis threshold of about −0.5 g. Use `not_upright` for the inverse
state. The component does not control indicator lights by itself.

If you need to read the orientation in a lambda, set `id: hatch_accel` on
`hatch_accelerometer` and check `id(hatch_accel).valid()` before calling
`id(hatch_accel).is_upright()`.

## XYZ diagnostics

Add these entries under `hatch_accelerometer` to see acceleration along each
axis in g. Compare the readings with the Hatch upright and tilted to see how
its orientation changes the Z-axis reading.

```yaml
  acceleration_x:
    name: Acceleration X
  acceleration_y:
    name: Acceleration Y
  acceleration_z:
    name: Acceleration Z
```

Disabling entities in Home Assistant or filtering their updates does not stop
I2C reads. Increase `update_interval` to read less often. To measure baseline
expander errors, compare against a build without `hatch_accelerometer` on the
same device.

## Detection and initialization

The default `model: AUTO` checks only `0x27` and `0x62`, starting on the first
poll. It selects a profile only if exactly one matches. If neither matches, it
probes again on each poll, up to 10 attempts, because the shared bus can be
busy right after boot. It then logs a warning and leaves the sensor
unconfigured. If both match, it logs a warning and stops at once. An absent
address can add some startup bus errors during those attempts. The component
does not poll that address again after detection ends. Set `model: DA213` or `model: MSA311` to check
only a known sensor. Both profiles require ID `0x13`. The MSA311 profile
rejects `0xDE` at register `0x11`, which stock firmware uses to identify the
MSA321. The MSA311 profile uses the stock `7.1.503` setup for `0x62` and reads
12-bit samples. The DA213 profile reads 14-bit samples.

`allow_initialization` defaults to `true`. The component checks for a supported
sensor before it writes the stock reset and configuration. It then samples at
`update_interval`, which defaults to `1s`. Set `allow_initialization: false` to
probe only. Probe-only mode does not write configuration or publish samples.
In [PR #7's hardware report][pr7], entities stayed unknown without
initialization. Do not rely on the stock configuration surviving a cold boot.

## Compile fixture

The [compile fixture](../esphome/hatch-accelerometer-check.yaml) wires all six
entities. It is not a hardware image; do not flash it.

```sh
python -m esphome compile esphome/hatch-accelerometer-check.yaml
python -m esphome -s accel_model DA213 compile esphome/hatch-accelerometer-check.yaml
python -m esphome -s accel_model MSA311 compile esphome/hatch-accelerometer-check.yaml
python -m pytest tests/test_hatch_accelerometer.py tests/test_hatch_expander_bus.py -q
```

[pr7]: https://github.com/w1ll1am23/hatch-riot-re/pull/7#issuecomment-6672390081
