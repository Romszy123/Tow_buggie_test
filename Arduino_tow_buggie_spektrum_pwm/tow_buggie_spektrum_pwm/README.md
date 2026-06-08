# Spektrum and BRemote Dual-VESC PWM Router

This is the Arduino IDE version of the current Arduino Mega dual-VESC router sketch.

## Pin Use

- `D49`: SRXL2 one-wire UART signal from the Spektrum receiver
- `D21`: BRemote RX `ESC_0 SIG` PWM input
- `D20`: BRemote RX `ESC_1 SIG` PWM input
- `D6`: Servo-style PWM output to VESC 0
- `D8`: Servo-style PWM output to VESC 1
- `GND`: Common ground between Arduino, BRemote RX, and both VESCs

`D20` is used instead of `D22` because the Arduino Mega supports external interrupts on `D20` and `D21`.

## CH5 Source Selection

- CH5 low: DXS CH1 controls throttle and DXS CH2 applies a 50% differential steering mix
- CH5 center: both VESC outputs receive minimum throttle
- CH5 high: BRemote input 0 passes to output 1 and input 1 passes to output 0

If fresh SRXL channel data is missing at startup or disappears later, the sketch immediately uses the BRemote inputs.

## Output Signal

Both VESC outputs are servo-style PWM:

- Minimum throttle: `996 us`
- Maximum throttle: `1984 us`
- Frame rate: `50 Hz`

## Serial Monitor

Open the serial monitor at `115200`. The sketch prints the active source, input values, and both output pulse widths at approximately 5 Hz.
