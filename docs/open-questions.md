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

## 6. Accelerometer bus

0x26 is known, but which bus it is on was not confirmed. **Resolve:** I2C scan.

## 7. RIoT+ power latch

The GreenPAK appears to hold the power latch and gate charging. Without matching handling, the unit could switch
off on battery or not charge. **Resolve:** decode `power_task_*` and the GreenPAK virtual-input usage before
porting the RIoT+.

## 8. RIoT LEDC override

On RIoT (hw type 1), `hal_check_riot_hardware` sets up an LEDC timer and channels from stack structs. These were not
decoded and may differ from the `LED_task_main` table.

## 9. Flash encryption / Secure Boot v1

You can't tell either from an OTA app image. **Resolve:** `espefuse.py summary` on the device.

## 10. Which retail product is which variant — RESOLVED

The [teardown](teardown.md) unit is model **HBREST2 = Hatch Rest (2nd gen)**, and it is the plain **"RIoT"**
variant (main board "RIoT Main V1.5"). So RIoT = Rest 2nd gen; by extension RIoT+ = Rest+ 2nd gen.
