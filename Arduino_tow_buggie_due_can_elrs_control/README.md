# Arduino IDE: Due ELRS/CRSF Control and SD Logging

This separate variant preserves the earlier logger-only sketch and adds ELRS takeover control, forward-only differential steering, link failsafe checks, and a low-throttle arming interlock.

Open:

```text
tow_buggie_due_can_elrs_control/tow_buggie_due_can_elrs_control.ino
```

`ENABLE_VESC_COMMANDS` remains `false` as the final bench-test gate. BRemote driving input is not yet integrated, so CH5 low commands both VESCs to zero.

Detailed wiring and bring-up instructions are in `tow_buggie_due_can_elrs_control/README.md`. The matching PlatformIO project is:

```text
../PlatformIO_tow_buggie_due_can_elrs_control
```

