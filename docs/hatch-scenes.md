# Scenes

`hatch_scenes` saves what the Hatch is doing into numbered slots and plays it back. A
scene is the night light (on or off, colour, white, brightness) and the speaker (what
it plays, Loop Sound, and volume). There are nine slots, 1–9.

Every action confirms itself on the clock for 3 seconds. A clock that is turned off
shows it at its dimmest.

| Action | Clock |
| --- | --- |
| Save | `SAV3` once slot 3 is on flash; `FULL` when no slot is empty |
| Load, next, previous | `FAV3` |
| Delete | `DEL3` |
| Empty slot, or no scenes saved | `NONE` |
| Flash write failed, or a sound that can't be saved | `ERR` |

## Setup

The Rest config ([`hatch-rest-base.yaml`](../esphome/hatch-rest-base.yaml)) already
includes it:

```yaml
hatch_scenes:
  id: scenes
  light_id: night_light
  media_player_id: player
  display_id: clock_display
```

An optional `current_scene` text sensor reports the slot loaded or saved last
(`1`–`9`, or `None`):

```yaml
  current_scene:
    name: Current Scene
```

The Rest config also adds Home Assistant actions: `save_scene`, `load_scene` and `delete_scene`
take a `slot`, and `next_scene` and `previous_scene` take none.

## Actions

```yaml
- hatch_scenes.save: scenes      # into the first empty slot
- hatch_scenes.save:
    slot: 3                      # into slot 3, replacing it
- hatch_scenes.load: 3
- hatch_scenes.next: scenes      # the next saved slot, wrapping round
- hatch_scenes.previous: scenes
- hatch_scenes.delete: scenes    # the slot loaded or saved last
- hatch_scenes.delete:
    slot: 3
```

`slot` also takes a lambda (`slot: !lambda return slot;`). This is how the Home Assistant
actions pass it through.

C++ lambdas can call the same methods: `id(scenes).save()`, `save(slot)`,
`load(slot)`, `next()`, `previous()`, `remove()`, `remove(slot)`, and
`current_slot()`.

## Touch ring

On the Rest and Rest+, the touch ring runs the scenes. Each touch acts when you let go:

| Touch | Result |
| --- | --- |
| Tap | Load the next scene |
| Hold 0.8 s | Turn off the night light and sound |
| Hold 5 s | Save a new scene. The clock shows `SAV?` from 5 s. |
| Hold 10 s | Delete the current scene. The clock shows `DEL?` from 10 s. |

A touch that you use with a front button (colour cycle, warmth, SD cache files)
does none of these. Toddler Lock turns the ring off like the other buttons.

The ring now does on the device what the
[touch ring favorites blueprint](../blueprints/automation/hatch-rest/touch-ring-favorites.yaml)
did in Home Assistant. That blueprint needs the Touch binary sensor of v1.5 and
earlier, which is now the Touch Ring event entity.

## What is saved

- The sound is the URI the speaker is playing: a stock sound
  (`hatch-library:///...`), a web URL, or nothing. A scene saved while nothing plays
  stops the speaker when loaded. A Music Assistant (Sendspin) stream can't be started
  again from the device, so it saves as nothing.
- A URI must be under 256 bytes. A longer one shows `ERR` and saves nothing.
- Each slot is written to flash as soon as it is saved or deleted, not at the next
  `flash_write_interval`. A flash write can stall audio for a moment.
- The current slot is not saved. After a restart, next starts at the first saved
  slot and previous at the last.
- Loading a scene doesn't update the Stock Sound select or the Loop Sound switch.
