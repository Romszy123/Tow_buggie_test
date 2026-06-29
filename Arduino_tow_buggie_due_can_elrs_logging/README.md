# Arduino IDE: Due Dual VESC UART, BMS CAN, ELRS, and SD Logger

This folder contains the Arduino IDE version of the Arduino Due controller.

Open this sketch in the Arduino IDE:

```text
tow_buggie_due_can_elrs/tow_buggie_due_can_elrs.ino
```

The controller:

- Decodes an ExpressLRS receiver using CRSF on `Serial2`.
- Controls and reads two VESCs over independent hardware UARTs.
- Reads the JK BMS over `CAN1`.
- Sends battery telemetry to the BRemote RX over `Serial1`.
- Logs VESC, BMS, ELRS, command, and communication-error data to microSD.

Detailed pinout, microSD wiring and retrieval, Native USB upload instructions, VESC Tool setup, BMS CAN wiring, and current safety status are documented in `tow_buggie_due_can_elrs/README.md`.

The matching VS Code/PlatformIO build is in:

```text
../PlatformIO_tow_buggie_due_can_elrs_logging
```
