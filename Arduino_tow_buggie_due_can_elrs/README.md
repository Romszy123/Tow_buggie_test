# Arduino IDE: Due CAN and ELRS Controller

This folder contains the Arduino IDE version of the Arduino Due controller.

Open this sketch in the Arduino IDE:

```text
tow_buggie_due_can_elrs/tow_buggie_due_can_elrs.ino
```

The controller:

- Decodes an ExpressLRS receiver using CRSF on `Serial2`.
- Communicates with two VESCs over `CAN0`.
- Reads the JK BMS over `CAN1`.
- Sends battery telemetry to the BRemote RX over `Serial1`.

Detailed pinout, CAN wiring, and current safety status are documented in `tow_buggie_due_can_elrs/README.md`.

The matching VS Code/PlatformIO build is in:

```text
../PlatformIO_tow_buggie_due_can_elrs
```
