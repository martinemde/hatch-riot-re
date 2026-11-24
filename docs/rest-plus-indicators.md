# Manual Rest+ 2 indicator lights

`hatch_plus_indicator` controls the three bottom indicator windows on the
Rest+ 2. The window names select hardware only. The lights do not follow Wi-Fi,
battery, or toddler lock status.

Add `hatch_plus_indicator` to the `external_components` list that provides
`hatch_plus_power`. If your existing power owner has `id: plus_power`, add
this entry to your `light:` list:

```yaml
light:
  - platform: hatch_plus_indicator
    name: Battery Indicator
    indicator: battery
    hatch_plus_power_id: plus_power
```

Choose `wifi`, `battery`, or `lock` for `indicator`. Configure at most one
light per window on each power owner. The light defaults to off at boot and
has no effects.

The RGB control produces only four physical states: off, red, green, and
yellow. Any nonzero red or green channel lights that color at full output.
Both channels together produce yellow. The light ignores blue. Turning the
light off or setting brightness to zero produces no output.

Only the [Rest+ power owner](rest-plus-power.md) writes to the GreenPAK. Writes
require `allow_control: true`, a programmed GreenPAK discovered at `0x10`, and
an active owner that is not preparing to shut down. In diagnostics-only mode,
commands do not change the physical light. The light platform does not enable
power control or NVM programming. Do not enable experimental power control
solely to test an indicator.
