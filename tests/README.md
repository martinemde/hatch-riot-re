# Host tests

Native unit tests for the custom ESPHome components. They compile the real component C++
against small stub ESPHome headers with the host C++ compiler and assert on behavior —
no ESP32, no ESP-IDF, no ESPHome install required.

## Running

```bash
pip install pytest
python -m pytest tests/
```

CI runs the suite against the pinned ESPHome release (the gate) and the latest release
(early warning, allowed to fail). Tests that need the ESPHome headers or a fetched managed
component skip themselves when those aren't available locally.

Each `tests/test_<area>.py` compiles `tests/host/<area>/<name>_test.cpp` together with the
component sources, using the stub headers under `tests/host/<area>/` (include paths are
per-area on purpose: every area stubs exactly the ESPHome surface its component needs,
e.g. the display area's `uart.h` records every frame written to the display MCU).

The optional shared `hatch_buttons` component, compile fixture and Home Assistant
blueprint checks are documented in [the button guide](../docs/hatch-buttons.md#testing-both-models).

## Adding a test

1. Create `tests/host/<area>/` with the stub headers the component includes.
2. Add a `<name>_test.cpp` with plain `assert()`s and a `main()`.
3. Add `tests/test_<area>.py` that compiles and runs it (copy `test_hatch_display.py`).
