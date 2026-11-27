# Rest+ 2 indicator outputs

`hatch_plus_indicator` drives the three bottom indicator windows on the
Rest+ 2: Wi-Fi, battery, and toddler lock. The window names select hardware
only. The [Rest+ package](rest-plus-2-esphome.md#status-indicators) already
configures all three and drives them like stock firmware. Use this component
directly only in a configuration without that package.

Each window is a red/green LED with no dimming, so it shows only off, red,
green, or yellow (both colours). The component has no Home Assistant entity.
Call `set_color()` from a lambda, for example from a template select:

```yaml
hatch_plus_indicator:
  - id: battery_indicator
    indicator: battery
    hatch_plus_power_id: plus_power

select:
  - platform: template
    name: Battery Indicator
    options: ["Off", "Red", "Yellow", "Green"]
    initial_option: "Off"
    optimistic: true
    on_value:
      - lambda: id(battery_indicator).set_color(static_cast<hatch_plus_indicator::Color>(i));
```

The option order must match `hatch_plus_indicator::Color`. Choose `wifi`,
`battery`, or `lock` for `indicator`. Configure at most one output per window
on each power owner. Each window is off at boot.

Only the [Rest+ power owner](rest-plus-power.md) writes to the GreenPAK. Writes
require `allow_control: true`, a programmed GreenPAK discovered at `0x10`, and
an active owner that is not preparing to shut down. In diagnostics-only mode,
the windows do not change. The component does not enable power control or NVM
programming. Do not enable experimental power control solely to test an
indicator.
