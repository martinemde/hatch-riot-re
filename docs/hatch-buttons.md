# Shared Hatch Rest / Rest+ button events

`hatch_buttons` classifies named press/release edges. It is optional and model-neutral:
no GPIO, light, media player, display, preferences, favorites or native actions.
Existing Rest firmware and its raw-touch favorites blueprint are unchanged.

## Setup on either model

Use ESPHome 2026.10.0 (the pinned CI gate). Add `hatch_buttons` to your
`external_components` list from this repository's `esphome/components`, then:

```yaml
hatch_buttons:
  id: buttons
  hold_time: 800ms
  multi_click_time: 350ms
  buttons:
    - name: touch
    - name: bottom
    - name: night_light
    - name: volume_up
    - name: volume_down
    - name: brightness_up
    - name: brightness_down
  on_button:
    - homeassistant.event:
        event: esphome.hatch_button
        data:
          device: ${name}
          button: !lambda return button;
          gesture: !lambda return gesture;
```

In each existing binary sensor, feed **both** edges using its matching name:

```yaml
on_press:
  - lambda: id(buttons).button_event("touch", true);
on_release:
  - lambda: id(buttons).button_event("touch", false);
```

Retain the existing sensor's pin configuration and debounce filters. Do not add a
second sensor on an occupied pin. Replace or deliberately retain its existing
actions: the classifier does not disable them, so retaining actions may cause
both raw-press behavior and classified automation behavior.

The existing [hardware map](hardware-map.md#buttons) and
[Rest base config](../esphome/hatch-rest-base.yaml) identify these inputs:

| Name | Existing input | Active level |
| --- | --- | --- |
| `touch` | GPIO33 | high |
| `bottom` | GPIO36 | low |
| `volume_up` | expander P0_0 (`number: 0`) | low |
| `brightness_up` | expander P0_1 (`number: 1`) | low |
| `night_light` | expander P0_2 (`number: 2`) | low |
| `volume_down` | expander P0_3 (`number: 3`) | low |
| `brightness_down` | expander P0_4 (`number: 4`) | low |

These are the existing Rest RIoT and Rest+ RIoT+ button input mappings, not a new
board definition. Touch still requires its existing expander P0_5 enable.
For **Rest**, edit a copy of the existing Rest configuration; preserve its
expander setup, touch filters and CONNECT/Improv behavior as needed.
For **Rest+**, add the component to a working, hardware-verified Rest+ config;
preserve its power/latch management and button input setup. Do not flash the
Rest image or the compile fixture to a Rest+ to test this component. The
[Rest+ power guide](rest-plus-power.md) describes that separate integration.
No new GPIO assignment or Rest+ hardware image is supplied here.

## Gesture contract

Names are configurable, nonempty and unique. Unknown names and duplicate edges
are ignored. The component expects debounced input and does not debounce itself.

- `tap` and `double` fire after `multi_click_time` since the last release.
- A press strictly before that timeout joins the sequence. At the exact timeout,
  it starts a new sequence even if the main loop has not run.
- `triple` fires immediately on the third short release. A fourth starts anew.
- `hold` fires **once at `hold_time` while pressed**. If the loop stalls, release
  still emits hold when duration is at least the threshold. Release never emits
  a second hold. This intentionally replaces the old PR's release-only hold.
- A short click followed by a hold emits the preceding tap/double, then hold.
- All buttons use the same rules. No repeat events or adjustment exceptions.
- Buttons have independent timers; unsigned elapsed-time arithmetic handles
  the millis counter wrapping around.

Defaults remain 800ms hold (allowed 1–60000ms) and 350ms multi-click
(allowed 1–2000ms). Timings measure the **debounced** edges, so sensor filters
also affect the physical timing observed by the user.

`set_child_lock(bool)` cancels pending clicks and active holds. An active press
remains cancelled through release even if unlocked first; it cannot replay.
Presses started while locked are also ignored through release.
`allow_buttons_when_locked: true` opts out of this policy. Wire the existing lock
switch's on/off handlers to `id(buttons).set_child_lock(true/false)` and initialize
it from any restored lock state at boot. This changes only shared events, not
other firmware behavior.

## Generic press/release hooks

Optional `on_press(button)`, `on_release(button, duration)` and `on_cancel(button)`
automations allow downstream logic without adding action policy to the classifier.
`duration` is a uint32 millisecond duration from the press, including time after a
hold. Cancel fires once for a pending sequence/active press when locked; no release
callback follows a cancelled press. Release callbacks run after hold classification
but before immediate triple classification. Hooks run synchronously and can lock
the component. Long-running work belongs in scripts, not blocking lambdas.

C++ uses namespace `esphome::hatch_buttons`, class `HatchButtons`,
`button_event(name, bool)`, `get_child_lock()` and `set_child_lock(bool)`.
Downstream components can register `add_on_button_callback`,
`add_on_press_callback`, `add_on_release_callback`, `add_on_cancel_callback` with
`std::function` signatures matching the YAML arguments above. There is no native
action mapping or persistence API.

## Home Assistant blueprint

[Hatch Rest / Rest+ buttons](../blueprints/automation/hatch/buttons.yaml) routes
`esphome.hatch_button` events for the seven conventional names to 28 optional
action lists. Use those names with the blueprint; custom names remain valid in
firmware and custom automations. Match `event_device_name` to the event's `device`
field. Enable Home Assistant actions in the ESPHome integration. Empty actions do
nothing. The optional blueprint lock gates HA actions only; use the firmware lock
to cancel pending gestures. The existing raw Rest blueprint is independent;
disable overlapping automations if you choose the classified-event blueprint.

## Testing both models

1. Compile your modified **Rest** or **Rest+** configuration before deployment.
2. On that model, observe `esphome.hatch_button` in HA Developer Tools → Events.
3. Test each wired input: one/two/three short presses and a hold past 800ms.
   Confirm one matching gesture, no extra tap after hold, and no native action
   unless you deliberately kept one.
4. Lock during a pending tap and during a hold, unlock before release, and
   verify neither sequence replays. Confirm a fresh press works afterward.
5. Repeat on the other model; a bench fixture does not verify physical wiring.

Local automated checks:

```sh
python -m pytest tests/ -q
python -m esphome compile esphome/hatch-buttons-check.yaml
# Use an existing HA environment, not the ESPHome environment:
/path/to/ha/python tests/validate_hatch_buttons_blueprint.py
```

The credential-free [compile fixture](../esphome/hatch-buttons-check.yaml) wires
fake template binary sensors to press/release and forwards classified events to
HA. It exercises all hooks but has no hardware pins: **never flash it**. Firmware
CI builds it on pinned/latest ESPHome (latest is allowed to fail). Host tests
cover thresholds, stale loops, wraparound, independent buttons, duplicate edges,
child lock and synchronous callback cancellation. Schema tests require ESPHome.
Compilation and tests do not claim physical-button or HA connectivity validation.
