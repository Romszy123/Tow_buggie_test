# Tow Buggie Controller Projects

This repository contains two tow buggie controller variants. Each variant is provided in two matching folders:

- An `Arduino_tow_buggie_...` folder for opening and compiling with the Arduino IDE.
- A `PlatformIO_tow_buggie_...` folder containing the same controller firmware arranged as a PlatformIO project for building, uploading, and monitoring directly from VS Code.

## Project Pairs

| Controller variant | Arduino IDE folder | PlatformIO folder |
|---|---|---|
| Arduino Mega, Spektrum SRXL2 and BRemote PWM router | `Arduino_tow_buggie_spektrum_pwm` | `PlatformIO_tow_buggie_spektrum_pwm` |
| Arduino Due, ELRS/CRSF, VESC CAN and JK BMS CAN | `Arduino_tow_buggie_due_can_elrs` | `PlatformIO_tow_buggie_due_can_elrs` |

The Arduino IDE and PlatformIO folders in each row contain equivalent firmware. Normally you edit and flash from one version rather than editing both independently.

Each folder has its own README with the relevant behavior, pinout, wiring, dependencies, and build instructions.

## Git Branches

- `master`: initial project snapshot.
- `feature/elrs-crsf-due`: Arduino Due CAN project with ELRS/CRSF receiver support and the reorganized project names.

Switching branches changes the files visible in this working folder. Git keeps the committed versions and branch history in the hidden `.git` directory.
