# Tow Buggie Due ELRS/CRSF Control + Logging

This is the separate ELRS control variant. The earlier logger-only projects remain unchanged.

## Safety status

- CRSF control and failsafe logic are implemented.
- `ENABLE_VESC_COMMANDS` is deliberately still `false`, so no motor-command packets are transmitted during bench checks.
- `VESC_UART_MAX_DUTY` remains limited to `0.20` for the first powered tests.
- BRemote control input is not yet integrated. When ELRS takeover CH5 is low, both requested motor commands are zero.

## ELRS channel behavior

| Function | Default ELRS channel | Behavior |
|---|---:|---|
| Throttle | CH1 | `<=1050 us` is idle; `1050..2012 us` maps to `0..1000` command |
| Steering | CH2 | Center `1500 us`, deadband `+/-40 us` |
| Takeover / arm | CH5 | Selected at `>=1700 us` |

The differential mix is forward-only: steering progressively reduces the inside wheel but never reverses it. After selection or link recovery, throttle must first return to idle before the controller arms.

The controller immediately disarms and commands zero if any of these occur:

- CRSF channel data is older than `250 ms`.
- CRSF link statistics are older than `500 ms`.
- Link quality is zero.
- CH5 takeover is released.

The defaults are defined near the top of the sketch and should be checked against the transmitter's Channel Monitor before motor commands are enabled.

## Arduino Due connections

The Due uses `3.3 V` logic. Do not apply a 5 V UART signal to a Due input.

| Function | Arduino Due pin | Connects to |
|---|---:|---|
| Left VESC TX/RX | `D1/TX0`, `D0/RX0` | Left VESC RX/TX |
| Right VESC TX/RX | `D14/TX3`, `D15/RX3` | Right VESC RX/TX |
| BMS CAN RX/TX | `DAC0/CAN1RX`, `D53/CAN1TX` | External 3.3 V-compatible CAN transceiver |
| BRemote telemetry TX/RX | `D18/TX1`, `D19/RX1` | BRemote RX `U1-0` UART |
| ELRS receiver TX | `D17/RX2` | Required CRSF control input |
| ELRS receiver RX | `D16/TX2` | Connected/reserved for CRSF telemetry |
| microSD chip select | `D4` | SD module `CS` |
| microSD SPI | Due 6-pin SPI header | `MOSI`, `MISO`, `SCK` |
| Upload/diagnostics | Native USB | PC when required |

All devices must share logic ground. Cross the CRSF UART: receiver TX to Due RX2 and receiver RX to Due TX2. The receiver is powered from the regulated 5 V rail; confirm the exact receiver's supply and UART levels before connecting it.

CRSF runs on `Serial2` at `420000 baud`. The RF band does not change this wiring or firmware. The schematic currently specifies 868 MHz; use a matching 868 MHz receiver, transmitter module, and antenna, and verify coexistence because the BRemote also operates in that band.

## Other interfaces

- Each VESC uses its own 115200-baud UART and packet parser.
- JK BMS CAN uses `CAN1` at `250 kbps` through an external transceiver, with 120-ohm termination at each physical bus end.
- `Serial1` sends BMS telemetry to the BRemote receiver; it does not yet receive BRemote driving commands.
- Native USB provides diagnostics without occupying `D0/D1`.

## microSD wiring and logging

Use a microSD module compatible with 3.3 V SPI. Connect `MOSI`, `MISO`, and `SCK` to the Due's 6-pin SPI header and `CS` to `D4`. Power the COM-MSD from the separate Due `3.3V` header; leave SPI header pins 2 (+5 V) and 5 (RESET) unconnected.

The firmware creates `LOGnnnn.CSV`, logs at 5 Hz, and flushes every 2 seconds. Send `E` through Native USB and wait for the close message before removing a powered card; send `R` after reinserting it.

## Safe bring-up sequence

1. Keep the drive wheels unloaded and leave `ENABLE_VESC_COMMANDS = false`.
2. Confirm all 16 CRSF channels and link quality in the Native USB monitor.
3. Verify CH1, CH2, and CH5 values, including transmitter failsafe behavior.
4. Confirm both VESC telemetry streams, BMS CAN data, and SD logs.
5. With VESC power isolated, confirm the displayed left/right commands, low-throttle re-arm, takeover release, and receiver power-off behavior.
6. Only then set `ENABLE_VESC_COMMANDS = true` and repeat at the 20% duty limit with wheels unloaded.
7. Add and test the BRemote control/fallback path before relying on two-source operation.

## Arduino IDE build

Open `tow_buggie_due_can_elrs_control.ino`, select **Arduino Due (Native USB Port)**, and install the `can_common`, `due_can`, and official Arduino `SD` libraries.

Use the Native USB connector for upload and `SerialUSB` diagnostics. The Programming USB circuitry shares `D0/D1`, which are assigned to the left VESC.

