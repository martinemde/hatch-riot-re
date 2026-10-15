"""speaker_source: local copy of ESPHome 2026.9.1's speaker_source media player.

Changes, marked "Hatch:" in the source:
- get_traits() also advertises REPEAT_SET, so Home Assistant offers repeat (one/off) for the player.
- Repeat one/off is also sent to the pipeline's simple sources (send_repeat_to_sources_()), so sd_cache can loop a
  file without a gap instead of going idle and being replayed.
The upstream class is `final` and builds its traits on every call, so it can't be extended from outside. Re-copy from
the ESPHome release when upgrading, and keep these patches.
"""
