# Tow Buggie Due CAN + ELRS PlatformIO Project

This branch replaces the planned Spektrum SRXL2 input with an ExpressLRS receiver using the CRSF serial protocol.

The project is currently a safe CAN/CRSF bring-up target:

- `CAN0` reads and commands the two Flipsky/VESC controllers.
- `CAN1` reads the JK BMS.
- `Serial1` sends simplified battery telemetry to the BRemote RX board.
- `Serial2` decodes all 16 CRSF channels from the ELRS receiver.
- The USB serial monitor prints raw and microsecond channel values for transmitter mapping.
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
| ELRS CRSF TX | `D16` / `TX2` | ELRS receiver `RX`, reserved for future telemetry |
| ELRS CRSF RX | `D17` / `RX2` | ELRS receiver `TX`, required |
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

### ELRS Receiver Wiring

The iFlight ExpressLRS Nano receiver uses full-duplex, non-inverted CRSF. Unlike Spektrum SRXL2, this uses a normal UART pair.

| ELRS Nano pad | Arduino Due connection | Required |
|---|---|---|
| `TX` | `D17` / `RX2` | Yes |
| `RX` | `D16` / `TX2` | Optional until ELRS telemetry is implemented |
| `5V` | Regulated `5V` supply | Yes |
| `GND` | Due `GND` / common logic ground | Yes |

The UART signals are crossed: receiver `TX` goes to Due `RX2`, and receiver `RX` goes to Due `TX2`.

The Due uses `3.3 V` logic. The Nano receiver is normally powered from `5 V`, while its CRSF UART signals are 3.3 V logic. Confirm the exact receiver revision before connecting it.

CRSF is configured at the normal ExpressLRS receiver rate:

```cpp
const uint32_t ELRS_BAUD = 420000;
```

The BRemote inputs remain reserved for the later source-selection stage:

| Planned function | Arduino Due pin |
|---|---:|
| BRemote PWM input 0 | `D21` |
| BRemote PWM input 1 | `D20` |

## Wiring Diagram

```text
                              Arduino Due
                           +----------------+
ELRS Nano TX ------------> | D17 / RX2      |
ELRS Nano RX <------------ | D16 / TX2      |  Future ELRS telemetry
BRemote PWM 0 ------------> | D21            |  Reserved
BRemote PWM 1 ------------> | D20            |  Reserved
                           |                |
CANRX/CANTX -------------- | CAN0           | --> 3.3 V CAN transceiver --> VESC 0 + VESC 1
DAC0/D53 ----------------- | CAN1           | --> 3.3 V CAN transceiver --> JK BMS
D18/D19 ------------------ | Serial1        | <--> BRemote RX U1-0 UART
                           +----------------+
```

## Identify The ELRS Channel Mapping

The sketch decodes CRSF frame type `0x16`, containing 16 packed 11-bit channels. Open the serial monitor at `115200 baud` and move one transmitter control at a time.

```text
ELRS fresh LQ=100% SNR=8 map(thr/steer/src)=988/1500/988us frames=1234 crcErr=0
CRSF channels: CH1=172/988us CH2=992/1500us ... CH16=992/1500us
```

Record the channels for throttle, steering, source selection, and any arm switch. Then update these one-based constants near the top of the sketch:

```cpp
const uint8_t ELRS_THROTTLE_CHANNEL = 1;
const uint8_t ELRS_STEERING_CHANNEL = 2;
const uint8_t ELRS_SOURCE_SELECT_CHANNEL = 5;
```

An ELRS control frame becomes stale after `250 ms`. Motor commands remain disabled, so receiver wiring and channel placement can be tested without driving either VESC.

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

`ENABLE_VESC_COMMANDS` is set to `false`. The first flash should only confirm CRSF decoding, CAN receive, serial output, and BRemote UART telemetry. Enable motor commands only after ELRS channel mapping, BRemote fallback, source selection, steering mix, arming, and failsafe behavior are implemented and tested.
