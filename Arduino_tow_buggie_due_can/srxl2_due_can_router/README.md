# Arduino Due CAN Router

This is the Arduino IDE version of the new Due-based tow buggie controller.

It is separate from the working Arduino Mega PWM router. The first version is a safe bring-up sketch: it initializes both CAN buses, parses VESC and JK BMS frames, prints telemetry, and sends a small battery telemetry packet to the BRemote RX over UART. Motor CAN commands are disabled by default in the sketch.

## Required Libraries

Install these Arduino libraries before compiling in the Arduino IDE:

- `can_common`: https://github.com/collin80/can_common
- `due_can`: https://github.com/collin80/due_can

## Pinout

The Arduino Due CAN pins are controller-level pins. Each CAN bus needs a 3.3 V-compatible CAN transceiver.

| Function | Arduino Due pin | Connects to |
|---|---:|---|
| VESC CAN RX | `CANRX` | VESC CAN transceiver `RXD` |
| VESC CAN TX | `CANTX` | VESC CAN transceiver `TXD` |
| BMS CAN RX | `DAC0` / `CAN1RX` | JK BMS CAN transceiver `RXD` |
| BMS CAN TX | `D53` / `CAN1TX` | JK BMS CAN transceiver `TXD` |
| BRemote telemetry TX | `D18` / `TX1` | BRemote RX `U1-0 RX` |
| BRemote telemetry RX | `D19` / `RX1` | BRemote RX `U1-0 TX`, optional |
| Ground | `GND` | Common logic ground |

## CAN Wiring

```text
Arduino Due CANRX/CANTX
        |
        v
3.3 V CAN transceiver
        |
        +-- CAN_H/CAN_L --> VESC 0
        |
        +-- CAN_H/CAN_L --> VESC 1

Arduino Due DAC0/D53
        |
        v
3.3 V CAN transceiver
        |
        +-- CAN_H/CAN_L --> JK BMS CAN
```

Use `120 ohm` termination at the two physical ends of each CAN bus. Do not add termination everywhere.

## Protocol Notes

The VESC bus uses the Flipsky/VESC extended-ID format:

```text
extended_id = vesc_id | (command_id << 8)
payload = big-endian
```

The JK BMS protocol uses a fixed `250 kbps` bus and little-endian payloads. The first useful frame is standard CAN ID `0x02F4`, which contains pack voltage, current and SOC.

## Current Status

- CAN0 is configured for the VESC bus at `500 kbps`.
- CAN1 is configured for the JK BMS bus at `250 kbps`.
- `Serial1` sends battery telemetry to the BRemote RX at `115200 baud`.
- VESC motor commands are disabled until the DXS/BRemote input migration and failsafe logic are added for the Due.

