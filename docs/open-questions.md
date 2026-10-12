# Open questions

These need a real device, or deeper static work, to settle.

## 1. Expander pin overlap

The enable outputs (LED = 0, SD = 1, amp = 2, touch = 9) and the button inputs (expander input bits 0–4) seem to
share pins 0–2.

Possible explanations:

- The button byte is read from a different register or port than assumed. `hal_check_riot_hardware` reads
  register 0 with length 1 into `0x3ffd8843`.
- The two chip options number their pins differently. AW9110B uses OUT0–OUT9 with ports A and B.

**Resolve:** on hardware, dump AW9523B registers 0x00/0x01 (inputs) while pressing each button, and toggle bits in
0x02/0x03 while watching the LEDs, amp and SD power.

**[O, hw] mostly resolved.** The chip is confirmed **AW9523B** (ID `0x23`), ruling out the AW9110B-numbering
explanation. Pressing each button one at a time (with the pins forced to input mode) shows the five front buttons
are **P0_0–P0_4, active-low**: Volume up = P0_0, Brightness = P0_1, Night light = P0_2, Volume down = P0_3, Touch
ring = P0_4 (first four timestamp-confirmed; P0_4 by elimination).

The key to the "overlap": on this unit the expander pins power up as **outputs reading 0x00**, so button presses
are invisible until the pins are switched to **input** — which is why direct presses registered nothing and the
probe's "all pins to INPUT" action was required. So the same P0_0–P0_2 pins the firmware uses as enable *outputs*
(LED/SD/amp) are the button *input* bits 0–2; the firmware must be multiplexing their direction, or the enable
lines are elsewhere than inferred. **Still open:** run the enable-side hypotheses (drive P0_0/P0_1/P0_2 high and
watch the LED/SD/amp) to see whether these pins truly double as enables or the enable mapping was misread.

## 2. RGBW channel order

ch0..ch3 = r, g, b, w is inferred from the data order. **Resolve:** light each channel by itself.

## 3. UART parity for the display

The config struct value is 3, which means `UART_PARITY_ODD`. This assumes the field order in IDF 5.5. **Resolve:**
try ODD, then NONE.

## 4. Display `misc` bits

`M & 0x07` likely drives the colon and indicator dots. **Resolve:** send 0–7 and watch.

## 5. Ambient light sensor

The firmware publishes ALS data to AWS IoT, but the sensor (I2C chip, or ADC through the ULP) was not found.
**Resolve:** I2C scan, and trace `als_data_send.c` callers.

**[O, hw]:** the one unexplained bus-0 device at **0x27** turned out to be the **accelerometer** (part ID `0x13`,
see #6), *not* the ALS. So there is no separate ALS I2C chip on this RIoT unit; the published ALS value is most
likely ADC/ULP-derived or simply absent on this variant. Still to resolve by tracing `als_data_send.c` callers.

## 6. Accelerometer bus — RESOLVED

The firmware uses 8-bit `0x4c` → 7-bit **0x26**. **[O, hw]** the bus-0 scan (GPIO0/GPIO22) showed no device at
0x26 but one at **0x27** — one address higher, i.e. the part's address pin is strapped high on this unit. A probe
register dump of 0x27 read **reg 0x01 (part ID) = `0x13`**, the WHO_AM_I of the **MiraMEMS MSA301 / DA213 (MSA3xx)
family** — one of the models `accelerometer_init` supports. So the accelerometer is on **bus 0 at 0x27** on this
unit. (Output regs 0x02–0x07 read 0/`--`: the probe does not put it into active sampling, and the bus was flaky
from an earlier brownout, but the ID is unambiguous.)

## 7. RIoT+ power latch

The GreenPAK appears to hold the power latch and gate charging. Without matching handling, the unit could switch
off on battery or not charge. **Resolve:** decode `power_task_*` and the GreenPAK virtual-input usage before
porting the RIoT+.

## 8. RIoT LEDC override

On RIoT (hw type 1), `hal_check_riot_hardware` sets up an LEDC timer and channels from stack structs. These were not
decoded and may differ from the `LED_task_main` table.

## 9. Flash encryption / Secure Boot — RESOLVED

`espefuse summary` on the device (via `scripts/backup.sh`) shows the eFuses **unlocked**: `FLASH_CRYPT_CNT=0`
(no flash encryption), `ABS_DONE_0=False` and `ABS_DONE_1=False` (no secure boot v1/v2), `UART_DOWNLOAD_DIS` and
`JTAG_DISABLE` both False, and no read/write-protect bits set. So the chip can be read, backed up (plaintext) and
reflashed over serial, and restored. This was for one unit (MAC `90:38:0c:…`); re-check any other unit before
flashing it.

## 10. Which retail product is which variant — RESOLVED

The [teardown](teardown.md) unit is model **HBREST2 = Hatch Rest (2nd gen)**, and it is the plain **"RIoT"**
variant (main board "RIoT Main V1.5"). So RIoT = Rest 2nd gen; by extension RIoT+ = Rest+ 2nd gen.
