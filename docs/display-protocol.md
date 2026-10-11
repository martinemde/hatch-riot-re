# Clock display protocol

The 4-digit 7-segment clock is driven by a **separate display microcontroller**. The firmware also prints its
version (`factory display fw minor: %s`). The ESP32 talks to it **one way over UART1**.

## UART setup

Set up in `hal_check_riot_hardware` (`0x40139542`–`0x401395b1`) [O]:

| Field | Value | Tag |
|---|---|---|
| Port | UART1 | [O] |
| TX | **GPIO27** | [O] `uart_set_pin(1, 27, -1, -1, -1)` |
| RX / RTS / CTS | none | [O] |
| Baud | **19200** (`0x4b << 8`) | [O] |
| Data bits | 8 (`UART_DATA_8_BITS = 3`) | [O] |
| Parity | value `3`, which is `UART_PARITY_ODD` | [O] value / [I] meaning. Assumes the IDF 5.5 `uart_config_t` field order. |
| Stop bits | 1 | [O] |
| Flow control | none | [O] |

Every write is wrapped in `clock_mutex_take` and followed by `uart_wait_tx_done(100)` and a 10-tick delay.

## Frames

### Set digits: `0x08`

`0x40138450` [O]:

```
08  S0 S1 S2 S3  M
```

- `S0..S3` = `font[ch - 0x20]` for the four characters of the display string. Characters outside `0x20..0xDF`
  give `0x00`.
- `M` = `misc & 0x07`. [I] These are the colon / indicator dots.

At boot the firmware sends `08 00 00 00 00 00` (blank).

### Brightness: `0x18`

`<seg_custom_brightness>` `0x401068d0` [O]:

```
18  B
```

`B = BRIGHTNESS[level]` for `level` 0..15. The table is at `0x3f432e0c`:

```
level: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
B:     fe 7e be 3e de 5e 9e 1e ee 6e ae 2e ce 4e 8e 0e
```

The serial command `TLRdd` (0–15) calls this with `15 - dd`. Brightness is saved in NVS.

A second, 21-entry table at `0x3f41a78c` is used by `0x40138078`, which also sends a `0x18` frame. It indexes
the table with `(x * 21) >> 16`, so it maps a 16-bit level (`x`) onto 21 steps. [I] This is probably brightness
that follows the light intensity; the caller wasn't traced.

```
88 84 8c 82 8a 86 8e 4e ce 2e ae 6e ee 1e 9e 5e de 3e be 7e fe
```

At boot the firmware sends `18 00`.

### Shutdown (I2C)

`<GPIO_Clock_IIC_shutdown>` `0x401380f4` writes the single byte `0x20` to I2C address **0x70** (8-bit `0xE0`).

## Font

From the table at `0x3f486910`, indexed by `ch - 0x20` with a 2-byte stride (only the first byte is used). Bit
layout is the standard `0bPGFEDCBA` (bit 7 = decimal point).

```
' '=00 !=86 "=22 #=7e $=6d %=d2 &=46 '=20 (=29 )=0b *=21 +=70 ,=10 -=40 .=80 /=52
0=3f 1=06 2=5b 3=4f 4=66 5=6d 6=7d 7=07 8=7f 9=6f :=09 ;=0d <=61 ==48 >=43 ?=d3
@=5f A=77 B=7c C=39 D=5e E=79 F=71 G=3d H=76 I=30 J=1e K=75 L=38 M=15 N=37 O=3f
P=73 Q=6b R=33 S=6d T=78 U=3e V=3e W=2a X=76 Y=6e Z=5b [=39 \=64 ]=0f ^=23 _=08
`=02 a=5f b=7c c=58 d=5e e=7b f=71 g=6f h=74 i=10 j=0c k=75 l=30 m=14 n=54 o=5c
p=73 q=67 r=50 s=6d t=78 u=1c v=1c w=14 x=76 y=6e z=5b {=46 |=30 }=70 ~=01
```

## ESPHome sketch

```yaml
uart:
  id: disp_uart
  tx_pin: GPIO27
  baud_rate: 19200
  data_bits: 8
  parity: ODD        # verify; try NONE if the display ignores frames
  stop_bits: 1

script:
  - id: show_time
    then:
      - lambda: |-
          static const uint8_t DIG[10] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
          auto t = id(ha_time).now();
          if (!t.is_valid()) return;
          int h = t.hour % 12; if (h == 0) h = 12;
          uint8_t f[6] = {0x08,
                          (uint8_t)(h >= 10 ? DIG[h / 10] : 0x00), DIG[h % 10],
                          DIG[t.minute / 10], DIG[t.minute % 10],
                          0x01 /* misc: colon? verify bit meaning */};
          id(disp_uart).write_array(f, 6);
```
