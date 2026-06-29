# Tow Buggie Due Dual VESC UART + BMS CAN + ELRS Logger

This branch replaces the planned Spektrum SRXL2 input with an ExpressLRS receiver using the CRSF serial protocol.

The project is currently a safe UART/CAN/CRSF bring-up target:

- Two VESCs use independent hardware UARTs.
- `CAN1` reads the JK BMS.
- `Serial1` sends simplified battery telemetry to the BRemote RX board.
- `Serial2` decodes all 16 CRSF channels from the ELRS receiver.
- A microSD card records VESC, BMS, ELRS, command, and communication-error data.
- The USB serial monitor prints raw and microsecond channel values for transmitter mapping.
- VESC motor commands are disabled by default in `src/tow_buggie_due_can_elrs.ino`.

## Build And Flash

From this folder:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run
```

To upload through the Due **Native USB port**:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run --target upload
```

To open the serial monitor:

```powershell
C:\Users\Romain\.platformio\penv\Scripts\pio.exe device monitor
```

## Arduino Due Pinout

The Arduino Due is a `3.3 V` board. Do not connect `5 V` logic directly to Due input pins.

The Due CAN1 controller still needs an external 3.3 V-compatible CAN transceiver for the JK BMS. PlatformIO uses the `dueUSB` board target so uploading and monitoring use Native USB.

### Active Pins In This Bring-Up Sketch

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
| ELRS CRSF TX | `D16` / `TX2` | ELRS receiver `RX`, reserved for future telemetry |
| ELRS CRSF RX | `D17` / `RX2` | ELRS receiver `TX`, required |
| microSD chip select | `D4` | SD module `CS` |
| microSD SPI | Due 6-pin SPI header | SD module `MOSI`, `MISO`, and `SCK` |
| Upload and diagnostics | Native USB port | PC, when required |
| Ground | `GND` | Common logic ground |

### CAN Transceiver Wiring

| Arduino Due side | CAN transceiver side |
|---|---|
| `DAC0` / `CAN1RX` | JK BMS bus transceiver `RXD` |
| `D53` / `CAN1TX` | JK BMS bus transceiver `TXD` |
| `3.3V` | Transceiver `VCC`, only if the module is 3.3 V compatible |
| `GND` | Transceiver `GND` |

| CAN bus side | Connects to |
|---|---|
| JK transceiver `CANH` | JK BMS `CANH` |
| JK transceiver `CANL` | JK BMS `CANL` |

Use one `120 ohm` termination at each physical end of the BMS CAN bus.

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
D1/D0 -------------------- | Serial         | <--> Left VESC UART
D14/D15 ------------------ | Serial3        | <--> Right VESC UART
DAC0/D53 ----------------- | CAN1           | --> 3.3 V CAN transceiver --> JK BMS
D18/D19 ------------------ | Serial1        | <--> BRemote RX U1-0 UART
Native USB --------------- | SerialUSB      | <--> PC upload and diagnostics
SPI header + D4 ---------- | SPI / CS       | <--> microSD module
                           +----------------+
```

## microSD Logging

Use a microSD module compatible with the Due's `3.3 V` SPI signals. Connect `MOSI`, `MISO`, and `SCK` to the Due 6-pin SPI header and `CS` to `D4`. Use a high-endurance FAT32 card for frequent logging.

Looking down at the top of the Due, with the USB connectors at the top, the SPI header below the main processor is:

```text
             Main processor

             5   3   1
             6   4   2

1 = MISO / CIPO       2 = +5V (do not use for COM-MSD)
3 = SCK               4 = MOSI / COPI
5 = RESET             6 = GND
```

Power the COM-MSD from the separate Due `3.3V` header. Leave SPI pins `2` and `5` unconnected.

The firmware creates a new `LOGnnnn.CSV` file at every startup, records at `5 Hz`, and flushes every `2 seconds`. Card failure does not stop the controller.

Logged data includes ELRS freshness and errors; both commands; VESC RPM, currents, duty, voltage, temperatures, faults, UART CRC/format/timeout counters; BMS voltage, current, SOC, temperatures, alarms, fault bits, and CAN age; and SD write errors.

Safe log retrieval:

- Power off before removing the card, or
- Send `E` through the Native USB serial monitor and wait for the close confirmation.
- Reinsert the card and send `R` to create a new file and resume logging.

Do not remove an active card. An unexpected power loss can lose up to the most recent two seconds.

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

## VESC UART Setup

Configure both VESCs for UART at `115200 baud`, connect each TX/RX pair crossed with a common ground, and use 3.3 V-compatible logic. The sketch follows Flipsky UART Protocol V1.0 and uses `COMM_SET_DUTY` plus `COMM_GET_VALUES`.

The left VESC uses `Serial` on `D1/D0`; the right VESC uses `Serial3` on `D14/D15`. Each has a separate packet parser and telemetry state.

`D0/D1` are associated with the Programming USB circuitry. Use the Native USB connector for upload and diagnostics. The sketch does not wait for `SerialUSB`, so standalone startup is unaffected. Disconnect the left VESC before using the Programming USB connector.

## JK BMS CAN Bus

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

`ENABLE_VESC_COMMANDS` is set to `false`, so UART motor-control packets are not sent. Telemetry requests and SD logging remain active. The first flash should only confirm CRSF decoding, BMS CAN receive, Native USB diagnostics, dual VESC UART telemetry, SD logging, and BRemote telemetry. Enable motor commands only after VESC input setup, ELRS channel mapping, BRemote fallback, source selection, steering mix, arming, and failsafe behavior are implemented and tested.
