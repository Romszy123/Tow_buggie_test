# PlatformIO: Spektrum and BRemote PWM Router

This is the PlatformIO build of the same Arduino Mega firmware found in:

```text
../Arduino_tow_buggie_spektrum_pwm/tow_buggie_spektrum_pwm
```

It reads Spektrum SRXL2 and BRemote inputs, then drives two servo-style VESC PWM outputs. See the Arduino sketch README for the detailed wiring and operating behavior.

Build it with:

```text
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run
```

Upload it to the Arduino Mega on `COM5` with:

```text
C:\Users\Romain\.platformio\penv\Scripts\pio.exe run --target upload
```

Open the serial monitor at `115200 baud` with:

```text
C:\Users\Romain\.platformio\penv\Scripts\pio.exe device monitor
```
