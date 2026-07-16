# Tow Buggie Controller Projects

This repository contains two tow buggie controller variants. Each variant is provided in two matching folders:

- An `Arduino_tow_buggie_...` folder for opening and compiling with the Arduino IDE.
- A `PlatformIO_tow_buggie_...` folder containing the same controller firmware arranged as a PlatformIO project for building, uploading, and monitoring directly from VS Code.

## Project Pairs

| Controller variant | Arduino IDE folder | PlatformIO folder |
|---|---|---|
| Arduino Mega, Spektrum SRXL2 and BRemote PWM router | `Arduino_tow_buggie_spektrum_pwm` | `PlatformIO_tow_buggie_spektrum_pwm` |
| Arduino Due, ELRS/CRSF control, dual VESC UART, JK BMS CAN and SD logging | `Arduino_tow_buggie_due_can_elrs_control` | `PlatformIO_tow_buggie_due_can_elrs_control` |

The Arduino IDE and PlatformIO folders in each row contain equivalent firmware. Normally you edit and flash from one version rather than editing both independently.

Each folder has its own README with the relevant behavior, pinout, wiring, dependencies, and build instructions.

## Git Branches

- `master`: current default project version, including the Due UART/CAN/ELRS controller and reorganized project names.
- `feature/elrs-crsf-due`: retained branch reference to the ELRS development work; it currently points to the same reorganization commit as `master`.

Switching branches changes the files visible in this working folder. Git keeps the committed versions and branch history in the hidden `.git` directory.
