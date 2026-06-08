# Tow Buggie Dual-VESC PWM Router

This folder contains the current Arduino Mega firmware for routing either a Spektrum DXS receiver or a BRemote RX board to two separate VESC PWM inputs.

## Projects

- `Arduino_tow_buggie_dual_vesc/srxl2_dual_vesc`: Arduino IDE version of the sketch
- `srxl2_dual_vesc_platformio`: PlatformIO version used to compile and flash directly from this workspace
- `Arduino_tow_buggie_due_can/srxl2_due_can_router`: Arduino IDE version of the new Arduino Due CAN bring-up sketch
- `srxl2_due_can_platformio`: PlatformIO version of the Arduino Due CAN bring-up sketch

The Mega projects contain the current working PWM router logic. The Due projects are separate and target the next CAN-based version with two VESCs on CAN0, the JK BMS on CAN1, and battery telemetry sent to the BRemote RX over UART.

On the `feature/elrs-crsf-due` branch, the Due projects decode an ExpressLRS Nano receiver over CRSF on `Serial2`: receiver `TX` connects to Due `D17/RX2`, and receiver `RX` connects to Due `D16/TX2`.

The PlatformIO projects are the easiest way to build and upload firmware directly from this workspace.

## Arduino Mega Pin Diagram

```text
Spektrum SRXL2 receiver
    single-wire SRXL2 signal ----------------------> D49

BRemote RX board
    ESC_0 SIG / PWM input 0 -----------------------> D21
    ESC_1 SIG / PWM input 1 -----------------------> D20

Arduino Mega
    D6  / PWM output 0 ----------------------------> VESC 0 signal input
    D8  / PWM output 1 ----------------------------> VESC 1 signal input

Arduino GND ---------------------------------------+--> BRemote RX GND
                                                  +--> VESC 0 GND
                                                  +--> VESC 1 GND
```

Use a common ground between the Arduino, BRemote RX board, and both VESCs. The PWM signal wires alone are not enough.

`D20` is used instead of `D22` because the Mega supports external interrupts on `D20` and `D21`. These interrupts are used to measure the two BRemote PWM input pulses reliably.

## Signal Routing

DXS channel 5 selects the active source for both VESC outputs:

| DXS CH5 position | VESC output behavior |
| --- | --- |
| Low | Use DXS: CH1 is throttle and CH2 applies differential steering |
| Center | Send minimum throttle to both VESCs |
| High | Use the BRemote PWM inputs |

The BRemote passthrough mapping is intentionally crossed to match the required steering direction:

```text
BRemote ESC_0 SIG on D21 --------------------------> VESC output 1 on D8
BRemote ESC_1 SIG on D20 --------------------------> VESC output 0 on D6
```

## DXS Steering Mix

When DXS mode is selected:

- CH1 controls the base throttle for both VESCs.
- CH2 applies a 50% differential steering mix.
- Full left lowers output 0 and raises output 1.
- Full right raises output 0 and lowers output 1.
- Both outputs remain constrained to the normal PWM range.

The measured DXS values used by the sketch are:

| Channel | Position | Value |
| --- | --- | ---: |
| CH1 | Minimum throttle | `708` |
| CH1 | Maximum throttle | `3336` |
| CH2 | Full right | `712` |
| CH2 | Center | `2048` |
| CH2 | Full left | `3396` |

## PWM Output

Both VESC outputs are servo-style PWM signals:

| Setting | Value |
| --- | ---: |
| Minimum throttle | `996 us` |
| Maximum throttle | `1984 us` |
| Frame rate | `50 Hz` |

If the DXS signal is absent at startup, becomes stale for more than `250 ms`, or enters failsafe, the sketch immediately defaults to the BRemote inputs.

## Serial Monitor

Use a serial monitor at `115200 baud`. The sketch prints the active source, relevant inputs, and both VESC outputs approximately five times per second.

DXS example:

```text
src=DXS CH1=2048 CH2=2048 out0=1500us out1=1500us
```

BRemote example:

```text
src=BRemote in0=1200us in1=1700us out0=1700us out1=1200us
```

## Compile And Flash With PlatformIO

The PlatformIO project is configured for an Arduino Mega 2560 on `COM5`. From the `srxl2_dual_vesc_platformio` folder:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

`pio run` compiles the firmware. `pio run --target upload` compiles and flashes it directly to the connected Arduino. The serial monitor command displays the live router status.

If upload reports a bootloader timeout, press the Mega's `RESET` button once as the upload begins.
