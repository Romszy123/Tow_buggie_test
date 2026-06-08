# Arduino IDE: Spektrum and BRemote PWM Router

This folder contains the Arduino IDE version of the Arduino Mega controller.

Open this sketch in the Arduino IDE:

```text
tow_buggie_spektrum_pwm/tow_buggie_spektrum_pwm.ino
```

The controller:

- Reads Spektrum SRXL2 channel data on `D49`.
- Reads two BRemote PWM inputs on `D21` and `D20`.
- Selects the source using Spektrum channel 5.
- Produces two servo-style VESC PWM outputs on `D6` and `D8`.

Detailed pinout and behavior are documented in `tow_buggie_spektrum_pwm/README.md`.

The matching VS Code/PlatformIO build is in:

```text
../PlatformIO_tow_buggie_spektrum_pwm
```
