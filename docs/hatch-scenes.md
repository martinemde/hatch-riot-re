# Optional shared scenes and favorites

The scenes component is independent. Optional physical-button composition below
requires the shared `hatch_buttons` PR (#9). Neither component changes
Rest or Rest+ firmware by default. The name is model-neutral; optional display
presets need a compatible `hatch_display` and entity wiring.

## What is stored

`hatch_scenes` owns ten local preference slots, indexed 0–9. A URI favorite
contains a URI, repeat flag, night-light RGB/white/brightness/on state, and media
volume. An empty URI is light-only and stops audio on recall. URI length is at
most 255 bytes. HTTP(S) and `hatch-library:///` are supported; local library paths
may contain spaces. `capture(index)` snapshots light and volume, not the currently
playing media URL: call `configure_favorite(index, uri, repeat)` first.

Optional `presets` capture stock-sound selection, repeat, clock enable, clock
light brightness/on state, display text, and a name as well. Preset names and
stock-sound labels have 32-byte limits; text has a 128-byte limit. Presets restore
through the configured select/switch/text entities, so their existing automations
must actually control the sound, clock, and text. The compile fixture uses dummy
entities and does not prove those device-specific automations.

Slots and selection persist in ESPHome preferences. Boot validates records and
restores selection only: it never turns on light or starts playback. The original
HPL2 key `0x48504C32`, version 2, record layout, and HPL1 migration key
`0x48504C31` are unchanged. Legacy eight-slot URI records migrate lazily, retaining
the old record for rollback. No entity name controls the storage key. Use one
`hatch_scenes` instance per device: multiple instances would share these keys.
Writes call preferences sync; a failed save returns false and does not show a
success message. This is not transactional rollback: an in-memory update can
remain after an unsuccessful write and be retried.

## Setup and button composition

1. Add `hatch_scenes` and `hatch_buttons` to your local external components.
2. Copy the scenes component block from `esphome/hatch-scenes-check.yaml`, replacing
   entity IDs with your existing light/media player and optional preset entities.
3. Use your model's documented binary sensors. On each press/release call
   `id(buttons).button_event("touch", true/false)` (and the other names).
   This component deliberately has no pin map or model-specific button list.
4. Keep all gesture classification in `hatch_buttons`. The example maps touch
   tap to next, bottom tap to previous, touch hold to stop, and night-light tap
   to toggle the light. Double/triple clicks have no scene action unless mapped.
5. Forward `night_light` raw press/release/cancel hooks to
   `management_press()`, `management_release(duration)`, and `management_cancel()`.
   Cancellation is required: it clears pending save/delete state when child lock
   activates. Set child lock on `buttons`, not on `scenes`. Direct scene API calls
   intentionally remain usable while physical controls are locked.

After #9 is available, merge this optional composition into your configuration:

```yaml
# Feed edges from the actual model's expander binary sensors; no pins are assumed here.
hatch_buttons:
  id: buttons
  buttons:
    - name: touch
    - name: bottom
    - name: night_light
  on_press:
    - lambda: |-
        if (button == "night_light") id(scenes).management_press();
  on_release:
    - lambda: |-
        if (button == "night_light") id(scenes).management_release(duration);
  on_cancel:
    - lambda: |-
        if (button == "night_light") id(scenes).management_cancel();
  on_button:
    - lambda: |-
        if (button == "touch" && gesture == "tap") id(scenes).next();
        if (button == "bottom" && gesture == "tap") id(scenes).previous();
        if (button == "touch" && gesture == "hold") id(scenes).stop();
        if (button == "night_light" && gesture == "tap") id(night_light).toggle().perform();

```

The shared classifier emits hold once at its threshold (default 800 ms), not on
release. Scene management instead waits for release and uses these fixed timings:

| Night-light press duration | Result on release |
| --- | --- |
| less than 800 ms | normal tap after shared multi-click window |
| 800–4999 ms | no scene action |
| 5000–9999 ms | save complete preset in first empty slot; `SAVn`, `FULL`, or `ERR` |
| 10000 ms or more | preview `DELn` at 10 s; delete that fixed slot on release |

A missing selected slot displays `NONE`. Deletion never happens during the
preview. Recalling another slot while a preview is visible does not change its
delete target. A stalled loop still checks the threshold on release. A full
store is never silently overwritten. Save/delete management requires all preset
entities; URI-only setups can use the explicit API without display management.
The stock 800 ms/350 ms classifier timings are configured in `hatch_buttons`;
scene save/delete remain 5/10 seconds, unchanged from the original extraction.

Volume and brightness adjustments belong in YAML, not in either C++ component.
Use the following hold-repeat pattern: start a script on press,
delay 800 ms, step every 150 ms, and stop on release/cancel. Apply one immediate
step on a release shorter than 800 ms; do not also map delayed `tap`, or it will
step twice. Use zero light transition for brightness steps. Do not route
adjustment repeats through `capture` or `save_current` (that would write flash).

For example, add `volume_up` to the shared button names and merge these handlers
into the existing blocks (repeat the pattern with opposite commands for volume
down and zero-transition light calls for brightness):

```yaml
script:
  - id: volume_up_repeat
    mode: restart
    then:
      - delay: 800ms
      - while:
          condition:
            lambda: return true;
          then:
            - media_player.volume_up: player
            - delay: 150ms
# Under hatch_buttons:
# on_press:
#   - if:
#       condition:
#         lambda: return button == "volume_up";
#       then:
#         - script.execute: volume_up_repeat
# on_release:
#   - if:
#       condition:
#         lambda: return button == "volume_up";
#       then:
#         - script.stop: volume_up_repeat
#         - if:
#             condition:
#               lambda: return duration < 800;
#             then:
#               - media_player.volume_up: player
# on_cancel:
#   - if:
#       condition:
#         lambda: return button == "volume_up";
#       then:
#         - script.stop: volume_up_repeat
```

Only the named button starts/stops that script. A long hold never performs the
short-release step. If your scripts can block the main loop, verify the boundary
on hardware: scripts are scheduling policy, not a second gesture classifier.

## Direct API

- `configure_favorite(index, uri, repeat)` configures a URI slot.
- `capture(index)` captures light/volume, or refreshes an existing full preset.
- `save_current()` captures the selected slot; false when none is selected.
- `store_preset(index, name)` captures a complete preset, including light/audio.
- `apply_preset(index)` restores a complete preset (or plays a URI slot).
- `play(index)`, `next()`, `previous()` recall only used slots and wrap around.
- `erase(index)` clears a slot; deleting the selected slot clears selection.
- `current_favorite()` returns selected index or -1. `stop()` turns off the
  night light and stops audio; it does not erase a slot.

Boolean results must be checked when exposed through your own automations.
Display feedback is an overlay; underlying clock/text updates are retained.
Source replacement, PCM muting, and real hardware playback remain the media
player's responsibility, not something these host tests prove.

## Automated verification

From the checkout with ESPHome installed:

```sh
python -m pytest tests/test_hatch_scenes.py -q
python -m esphome config esphome/hatch-scenes-check.yaml
python -m esphome compile esphome/hatch-scenes-check.yaml
```

The host tests compile the real controller with boundary doubles for
ESPHome preferences, entities, and display. They exercise record validation,
URI recall, persistence failures, reboot without playback, full preset capture,
legacy migration, 5/10-second boundaries, fixed-target deletion, cancellation,
wraparound, full storage, and a stalled loop. They do not contact hardware or
network. The fixture is compile-only with dummy outputs and example pins: never
flash it to a Hatch. It compiles scenes without a shared-buttons dependency. Separate schema tests
require light/media IDs and every entity in an optional presets block. The basic
URI controller host binary also compiles without the presets feature define.

## Manual test on an actual device

Use an existing verified device configuration, compile it, and arrange a safe
maintenance window before flashing. Do not use fixture pins.

1. Start from known slots (erase only slots you are willing to lose). Choose a
   stock sound, volume, night-light color/white/brightness, clock brightness and
   enable state, and text. Hold night-light 5–9 seconds, then release. Expect
   `SAV0` for an empty store, and no action before release.
2. Change every setting. Tap touch once; wait the multi-click window. Expect the
   saved light, sound, repeat, volume, clock, and text. If the saved sound is
   `None`, audio must stop. Confirm directly on the device, not only entity state.
3. Save a second distinct scene. Touch tap goes forward; bottom tap goes back;
   both skip empty slots and wrap. Double/triple touch must not also advance.
4. Hold touch for 800 ms. Expect stop at the hold threshold, once only, with no
   delayed next action on release. Tap night-light toggles without saving.
5. Hold night-light less than 5 seconds (but past 800 ms): no save. Hold exactly
   past 5 seconds: release saves. Fill ten slots: another 5-second hold shows
   `FULL` and preserves every slot.
6. Select a slot, hold night-light past 10 seconds, and verify `DELn` preview.
   Release deletes it. If you select another slot remotely while previewing,
   release must delete the original preview target, not the newer selection.
7. During a pending save/delete hold enable child lock. Expect preview cleared
   and no save/delete on release. Unlock while still holding: that same physical
   press must remain cancelled until released. Start a new press to act again.
8. Try volume/brightness short taps and holds during playback. Expect one
   immediate short-tap step, repeat only after 800 ms and at 150 ms intervals,
   no extra release step, bounded values, and no favorite flash writes. Child
   lock must stop the repeat script as well as scene callbacks.
9. Reboot. Confirm selected slot survives but light/audio do not start. Recall
   each slot and verify all settings again. Power-loss durability and actual
   audio transition behavior require this hardware test; compile is not proof.

No device was flashed or manually verified as part of this extraction.
