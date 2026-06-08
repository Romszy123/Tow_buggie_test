# Tow Buggie Due CAN PlatformIO Project

This PlatformIO project is the Arduino Due version of the tow buggie controller. It is separate from the existing Arduino Mega PWM router.

The project is currently a CAN/UART bring-up target:

- `CAN0` reads and commands the two Flipsky/VESC controllers.
- `CAN1` reads the JK BMS.
- `Serial1` sends simplified battery telemetry to the BRemote RX board.
- VESC motor commands are disabled by default in `src/srxl2_due_can_router.ino`.

## Build And Flash

From this folder:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run
```

To upload to the Due on `COM5`:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run --target upload
```

To open the serial monitor:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe device monitor
```

## Arduino Due Pinout

The Arduino Due is a `3.3 V` board. Do not connect `5 V` logic directly to Due input pins.

The Due has CAN controllers, but it still needs external CAN transceivers. Use 3.3 V-compatible CAN transceivers only.

### Active Pins In This Bring-Up Sketch

| Function | Arduino Due pin | Connects to |
|---|---:|---|
| VESC CAN RX | `CANRX` | VESC CAN transceiver `RXD` |
| VESC CAN TX | `CANTX` | VESC CAN transceiver `TXD` |
| BMS CAN RX | `DAC0` / `CAN1RX` | JK BMS CAN transceiver `RXD` |
| BMS CAN TX | `D53` / `CAN1TX` | JK BMS CAN transceiver `TXD` |
| BRemote telemetry TX | `D18` / `TX1` | BRemote RX `U1-0 RX` |
| BRemote telemetry RX | `D19` / `RX1` | BRemote RX `U1-0 TX`, optional |
| Ground | `GND` | Common logic ground |

### CAN Transceiver Wiring

| Arduino Due side | CAN transceiver side |
|---|---|
| `CANRX` | VESC bus transceiver `RXD` |
| `CANTX` | VESC bus transceiver `TXD` |
| `DAC0` / `CAN1RX` | JK BMS bus transceiver `RXD` |
| `D53` / `CAN1TX` | JK BMS bus transceiver `TXD` |
| `3.3V` | Transceiver `VCC`, only if the module is 3.3 V compatible |
| `GND` | Transceiver `GND` |

| CAN bus side | Connects to |
|---|---|
| VESC transceiver `CANH` | VESC 0 `CANH` and VESC 1 `CANH` |
| VESC transceiver `CANL` | VESC 0 `CANL` and VESC 1 `CANL` |
| JK transceiver `CANH` | JK BMS `CANH` |
| JK transceiver `CANL` | JK BMS `CANL` |

Use one `120 ohm` termination at each physical end of each CAN bus.

### Reserved For Control Input Migration

These pins are reserved to keep the wiring close to the current Mega project, but they are not active in this first CAN bring-up sketch yet.

| Planned function | Arduino Due pin | Notes |
|---|---:|---|
| Spektrum SRXL2 single-wire data | `D49` | Needs a SAM3X-compatible SRXL2 single-wire input layer |
| BRemote PWM input 0 | `D21` | Planned fallback/control input |
| BRemote PWM input 1 | `D20` | Planned fallback/control input |

## Wiring Diagram

```text
                         Arduino Due
                      +----------------+
Spektrum SRXL2  ----> | D49            |  TODO: SAM3X single-wire layer
BRemote PWM 0   ----> | D21            |  TODO: migrate from Mega project
BRemote PWM 1   ----> | D20            |  TODO: migrate from Mega project
                      |                |
CANRX/CANTX     ----> | CAN0           | --> 3.3 V CAN transceiver --> VESC 0 + VESC 1
DAC0/D53        ----> | CAN1           | --> 3.3 V CAN transceiver --> JK BMS
D18/D19         ----> | Serial1        | --> BRemote RX U1-0 UART
                      +----------------+
```

## CAN Bus Setup

### VESC Bus

The VESC bus is configured for `500 kbps` in the sketch:

```cpp
const uint32_t VESC_CAN_BAUD = CAN_BPS_500K;
```

Set both VESCs to the same CAN baud rate in VESC Tool. Give each VESC a unique CAN ID:

| VESC | CAN ID |
|---|---:|
| Left | `1` |
| Right | `2` |

The sketch uses the Flipsky/VESC extended-ID format:

```text
extended_id = vesc_id | (command_id << 8)
payload = big-endian
```

Useful frames:

| Command | ID | Direction | Meaning |
|---|---:|---|---|
| `CAN_PACKET_SET_CURRENT_REL` | `0x0A` | Due -> VESC | Relative current command |
| `CAN_PACKET_STATUS` | `0x09` | VESC -> Due | RPM, motor current, duty |
| `CAN_PACKET_STATUS_4` | `0x10` | VESC -> Due | FET temp, motor temp, battery current |
| `CAN_PACKET_STATUS_5` | `0x1B` | VESC -> Due | Tacho, input voltage |

### JK BMS Bus

The JK BMS bus is configured for `250 kbps`:

```cpp
const uint32_t JK_BMS_CAN_BAUD = CAN_BPS_250K;
```

The JK protocol uses little-endian payloads. Useful frames:

| CAN ID | Frame type | Meaning |
|---:|---|---|
| `0x02F4` | Standard | Pack voltage, current, SOC |
| `0x05F4` | Standard | Max/min/average cell temperature |
| `0x07F4` | Standard | Alarm levels, event based |
| `0x18F128F4` | Extended | Capacity details |
| `0x18F328F4` | Extended | Fault bit flags |

## BRemote UART Packet

The Due sends this binary packet on `Serial1` every `500 ms`:

```text
A5 5A
version              1 byte
flags                1 byte, bit0 = BMS data fresh
pack_mV              uint16 little-endian
pack_current_dA      int16 little-endian
soc_percent          uint8
avg_temp_C           int8
alarm_level          uint8
crc16_ccitt          uint16 little-endian over bytes 0-10
```

The BRemote RX firmware still needs a matching parser on its `U1-0` UART channel.

## Safety Defaults

`ENABLE_VESC_COMMANDS` is set to `false`. The first flash should only confirm CAN receive, serial output, and UART telemetry. Enable motor commands only after the Due SRXL2 input layer, BRemote fallback, CH5 selection, and failsafe behavior are implemented and tested.
