# Optional Hatch DA213 accelerometer

On a Hatch fitted with the [MiraMEMS DA213](hardware-map.md#accelerometer-da213),
use the existing `hatch_expander` on stock **I2C0**: GPIO0 (SDA) and GPIO22
(SCL). The expander is at `0x58`; the DA213 is at 7-bit address `0x27` and
returns `0x13` in register `0x01`. On Rest+, the native I2C1 bus on GPIO13/23
is for power peripherals, not the accelerometer. Do not create a second I2C
master on GPIO0/22.

In your Rest+ config, add `hatch_accelerometer` alongside `hatch_expander` in
the existing `external_components` list. Add the following block, replacing
`io_exp` if your GPIO0/22 expander has a different ID:

```yaml
hatch_accelerometer:
  id: hatch_accel
  hatch_expander_id: io_exp
  allow_initialization: true
  tilt_angle:
    name: Tilt Angle
    filters:
      - delta: 2.0
  upright:
    name: Upright
    disabled_by_default: true
```

By default `allow_initialization` is false: the component only checks the
DA213 ID and does not publish samples. With initialization enabled, it checks
the ID before applying the stock reset and configuration, then samples at
`update_interval` (default `1s`). In [PR #7's hardware report][pr7], Rest
entities stayed unknown until initialization was enabled. Do not rely on the
stock configuration surviving a cold boot.

Tilt Angle is disabled by default in Home Assistant. Enable it there if you
want the angle, or set `disabled_by_default: false` in YAML for new installs.
The example's delta filter reduces updates from the reported ±1° jitter.
Disabling the entity or filtering its updates does not stop I2C reads. To
reduce reads, increase `update_interval`. To measure baseline expander errors,
compare against a build without `hatch_accelerometer` on the same device.
This change was not flashed or tested on hardware.

Optional diagnostic entity keys are `acceleration_x`, `acceleration_y`,
`acceleration_z` (g), `tilt_angle` (degrees), `upright` and `not_upright`.
Upright follows the stock Z-axis threshold (about −0.5 g). In a lambda, check
`id(hatch_accel).valid()` before using `id(hatch_accel).is_upright()`. The
component does not control indicator lights by itself.

The [compile fixture](../esphome/hatch-accelerometer-check.yaml) wires all six
entities. It is not a hardware image; do not flash it.

```sh
python -m esphome compile esphome/hatch-accelerometer-check.yaml
python -m pytest tests/test_hatch_accelerometer.py tests/test_hatch_expander_bus.py -q
```

[pr7]: https://github.com/w1ll1am23/hatch-riot-re/pull/7#issuecomment-6672390081
