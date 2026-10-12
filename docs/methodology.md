# Methodology

## Toolchain

There was no Ghidra, IDA or Hopper on the analysis machine, so the work used **Capstone 6** (Xtensa support) and
the small scripts in `tools/`.

| Script | Purpose |
|---|---|
| `split_segments.py` | Parse the ESP32 image header and dump each load segment |
| `dis.py` | Find functions (4-byte-aligned `entry a1,N` opcodes) and disassemble each one instruction at a time. Resolves `l32r` literals and annotates C strings. |
| `xr.py` | Build a caller/callee index. Auto-names functions by the identifier-like strings they reference (`__func__` from `ESP_LOGx` / `ESP_RETURN_ON_FALSE`). |
| `q.py` | Queries: `byname`, `callers`, `ctx` (instructions before each call, to read arguments), `dump` |
| `nm.py` | Dump a function with call targets annotated by auto-name |
| `dd.py` | Collapse the triplicated stores emitted by `-mfix-esp32-psram-cache-issue` |
| `fof.py` | Find the function containing an address |
| `mem.py` | Read words from constant data (config structs) |

### Ghidra (added 2026-10-12)

Ghidra 12.1.4 decompiles Xtensa out of the box. `tools/img2elf.py` turns the app image into an ELF (segments at
their load addresses, `xr.py` auto-names as symbols, and with `--rom` the ESP-IDF mask-ROM names), and
`tools/ghidra/ExportDecomp.java` writes decompiled C for chosen functions from a headless run:

```sh
python3 -I tools/img2elf.py firmware/riot_*.bin work/riot_7_1_644.elf work/listing.txt.xr \
    --rom ~/.cache/esphome/idf/frameworks/5.5.5/components/esp_rom/esp32/ld
GH=~/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
$GH work/ghidra riot -import work/riot_7_1_644.elf -overwrite                    # once (~1 min)
$GH work/ghidra riot -process riot_7_1_644.elf -noanalysis -scriptPath tools/ghidra \
    -postScript ExportDecomp.java work/ghidra/out.c 401c70d4 401c73bc            # no addresses = all
```

Caveat: Ghidra collapses Xtensa zero-overhead `loop` bodies to a single iteration in pseudocode. Check loop
contents against data tables or the disassembly. (The REA MCP tools reject Xtensa ELFs.)

## Xtensa / Capstone gotchas

- Capstone 6 does **not** decode `beqz.n`, `bnez.n` or `nop.n` (and a few other narrow ops). Left alone, this
  desyncs the linear sweep. `dis.py` decodes them by hand: `op0 = 0xC` with `t[3] = 1` for the branches, and the
  `op0 = 0xD`, `r = 0xF` group.
- Capstone prints `call8 . +off` with the offset already scaled. The target is `(pc & ~3) + off`.
- Literal pools sit among the code in IROM, so some `.byte` / garbage instructions after unconditional jumps are
  expected.
- Every store in this image appears 3× (PSRAM cache-bug workaround). `dd.py` hides the repeats.

## How the hardware map was recovered

1. **String inventory:** `strings` on DROM/DRAM. Source paths, `__func__` names and log formats gave the component
   list (GreenPAK, AW9523B/AW9110B, MiraMEMS accelerometers, I2S, SDMMC, LEDC, ADC, ULP).
2. **Find IDF driver entry points** by their own `__func__` strings, e.g. `i2c_param_config` `0x401df010`,
   `ledc_channel_config` `0x401a5608`, `ledc_timer_config` `0x401a5198`, `i2s_channel_init_std_mode`
   `0x4019ebac`, `gpio_set_level` `0x400e362c`, `adc1_config_channel_atten` `0x401a5fe0`.
3. **Read arguments at call sites** (`q.py ctx`). Configs are either built on the stack (follow the `s32i` /`s16i`
   stores at offsets from `a1`) or passed as pointers to DRAM structs (read with `mem.py`). Field offsets come from
   the IDF v5.5 headers.
4. **Follow the HAL layer:** `hal_check_riot_hardware` `0x40139334` does variant detection, bus bring-up, expander,
   UART display, LEDC/ADC and GreenPAK checks in one place.

## Limits

- Static analysis only; nothing was run.
- Function boundaries come from heuristics, so some functions may be split or merged.
- No types or decompiler output; struct layouts were applied by hand.
- For deeper work (power task, accelerometer setup), load the image in **Ghidra 11+** with the Xtensa/ESP32
  loader for proper decompilation.
