# Arduino Due Dual VESC UART + BMS CAN + ELRS Logger

This is the Arduino IDE version of the Due and ExpressLRS CRSF bring-up controller.

Both VESCs use independent hardware UARTs, CAN1 remains dedicated to the JK BMS, and troubleshooting data is written to a microSD card. Diagnostics use the Native USB port, and the controller starts normally without a USB connection. Motor commands are disabled by default.

## Required Libraries

Install these Arduino libraries before compiling in the Arduino IDE:

- `can_common`: https://github.com/collin80/can_common
- `due_can`: https://github.com/collin80/due_can
- `SD`: install the official Arduino SD library from Library Manager

## Pinout

The JK BMS CAN pins are controller-level pins and need a 3.3 V-compatible CAN transceiver.

| Function | Arduino Due pin | Connects to |
|---|---:|---|
| Left VESC UART TX | `D1` / `TX0` | Left VESC UART RX |
| Left VESC UART RX | `D0` / `RX0` | Left VESC UART TX |
| Right VESC UART TX | `D14` / `TX3` | Right VESC UART RX |
| Right VESC UART RX | `D15` / `RX3` | Right VESC UART TX |
| BMS CAN RX | `DAC0` / `CAN1RX` | JK BMS CAN transceiver `RXD` |
| BMS CAN TX | `D53` / `CAN1TX` | JK BMS CAN transceiver `TXD` |
| BRemote telemetry TX | `D18` / `TX1` | BRemote RX `U1-0 RX` |
| BRemote telemetry RX | `D19` / `RX1` | BRemote RX `U1-0 TX`, optional |
| ELRS CRSF TX | `D16` / `TX2` | ELRS receiver `RX`, future telemetry |
| ELRS CRSF RX | `D17` / `RX2` | ELRS receiver `TX`, control data |
| microSD chip select | `D4` | SD module `CS` |
| microSD SPI | Due 6-pin SPI header | SD module `MOSI`, `MISO`, and `SCK` |
| Upload and diagnostics | Native USB port | PC, when required |
| Ground | `GND` | Common logic ground |

All devices must share logic ground. The Due is a 3.3 V device; do not apply a 5 V UART signal directly to its RX pin.

## microSD Logging

Use a microSD module that accepts the Due's `3.3 V` SPI logic. Connect `MOSI`, `MISO`, and `SCK` to the Due's 6-pin SPI header, `CS` to `D4`, and connect power and ground according to the module specification. A high-endurance card formatted as FAT32 is recommended.

Looking down at the top of the Due, with the USB connectors at the top, the SPI header below the main processor is:

```text
             Main processor

             5   3   1
             6   4   2

1 = MISO / CIPO       2 = +5V (do not use for COM-MSD)
3 = SCK               4 = MOSI / COPI
5 = RESET             6 = GND
```

Connect the COM-MSD `3v3` pin to the separate Due `3.3V` power header. Leave SPI pins `2` and `5` unconnected.

At each startup, the firmware creates the first unused filename from `LOG0000.CSV` through `LOG9999.CSV`. It records at `5 Hz` and flushes buffered data every `2 seconds`. A missing or failed card does not stop motor, receiver, BMS, or VESC communication.

The CSV contains:

- ELRS freshness, frame count, CRC errors, and left/right commands.
- Left and right VESC RPM, motor/input current, duty, voltage, temperatures, and fault code.
- Independent VESC UART valid-packet, CRC, format, and timeout counters.
- BMS freshness, CAN frame count, voltage, current, SOC, temperatures, alarms, and fault bits.
- SD write-error count.

To retrieve logs:

1. Safest method: power the controller off, remove the card, and open the CSV files on a computer.
2. While connected through Native USB, send `E` in the serial monitor. Wait for `SD log closed`, then remove the card.
3. After reinserting the card, send `R` to create a new log and resume logging.

Do not remove the card while the log is active. At most the most recent two seconds may be missing after an unexpected power loss.

## VESC Tool Setup

- Enable the UART application at `115200 baud`, `8N1`.
- Connect each Due TX to the corresponding VESC RX and each Due RX to the corresponding VESC TX.
- Connect the Due and both VESC logic grounds.
- Do not connect a 5 V UART signal or the VESC 5 V output to a Due I/O pin.
- The sketch uses Flipsky UART Protocol V1.0 short frames, `COMM_SET_DUTY`, and `COMM_GET_VALUES`.

UART control is normalized from `0.0` stop to `1.0` full configured duty. `VESC_UART_MAX_DUTY` can limit the maximum duty during bring-up.

## USB And Uploading

`D0/RX0` and `D1/TX0` are reserved for the left VESC. They are also associated with the Due Programming USB circuitry.

- Select **Arduino Due (Native USB Port)** in the Arduino IDE.
- Connect the PC to the **Native USB** connector for uploading and `SerialUSB` diagnostics.
- Do not use the Programming USB serial monitor while the left VESC is connected.
- The sketch does not wait for `SerialUSB`, so it runs normally when no PC is attached.
- If the Programming USB connector must be used, disconnect the left VESC from `D0/D1` first.

## BMS CAN Wiring

```text
Arduino Due DAC0/D53
        |
        v
3.3 V CAN transceiver
        |
        +-- CAN_H/CAN_L --> JK BMS CAN
```

Use `120 ohm` termination at the two physical ends of the BMS CAN bus.

## Protocol Notes

The JK BMS protocol uses a fixed `250 kbps` bus and little-endian payloads. The first useful frame is standard CAN ID `0x02F4`, which contains pack voltage, current and SOC.

## Current Status

- CAN1 is configured for the JK BMS bus at `250 kbps`.
- The left VESC uses `Serial` on `D1/D0` at `115200 baud`.
- The right VESC uses `Serial3` on `D14/D15` at `115200 baud`.
- Both VESC UART replies are parsed independently for RPM, current, duty, voltage, and temperature.
- Troubleshooting data is logged to microSD on `D4` plus the Due SPI header.
- `Serial1` sends battery telemetry to the BRemote RX at `115200 baud`.
- `Serial2` decodes 16 ELRS CRSF channels at `420000 baud`.
- `SerialUSB` prints every channel and device status through the Native USB port.
- VESC motor commands remain disabled until the input configuration, ELRS/BRemote source selection, and failsafe logic are verified.
