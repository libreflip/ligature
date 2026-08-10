# Production firmware baseline

The `production` PlatformIO environment is the first host-testable Ligature
production-firmware baseline. Its platform-independent core is in
`include/production/controller.h` and `src/production/controller.cpp`; the ESP32
adapter is in `src/production/main.cpp`.

## Safety status

This image is **not production-ready and must not be used for powered lift-axis
motion yet**.

`include/production/commissioned_config.h` deliberately has
`commissioned=false` and an invalid distance conversion. The image therefore
boots `COMMISSIONING_ONLY`, keeps PWM off, and rejects `M3`. Before that marker
can change, the assembled lift axis still needs:

- a confirmed endstop GPIO and polarity (`LIGATURE_ENDSTOP_PIN` is currently
  unset);
- the accepted assembled-axis distance conversion, bottom limit, tuning,
  homing, touchdown, and press-level values;
- replacement of the isolated provisional transient-current and low-speed
  policies after their wayfinding decisions resolve;
- a cooperative, interruptible `M40` adapter. The baseline core exposes the
  alignment effect and result seam, but the ESP32 adapter fails `M40` closed
  rather than invoking SimpleFOC's blocking `initFOC()`;
- a bounded native SimpleFOC Studio adapter. This baseline accepts only the
  strict production dialect; Studio traffic cannot fall through and cannot
  arm or move the motor;
- device verification of boot/reset PWM behavior, stored electrical
  calibration, Hall feedback, current feedback, endstop behavior, motion,
  homing, touchdown, hold, stop latency, serial scheduling, and capture timing.

No upload or physical test is evidence for this baseline. Existing Hall,
smoke-test, and geared-motor evidence has not validated the assembled lift axis.

## Offline conformance

Build and run the host harness with an ordinary C++ compiler:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Iinclude \
  src/production/controller.cpp \
  test/production_conformance/test_main.cpp \
  -o /tmp/ligature-production-conformance
/tmp/ligature-production-conformance
```

The harness covers fail-closed configuration, strict arm parsing, trust gates,
the one-command relative override, homing and the negative-Z frame, soft-limit
rejection, travel completion, `M53`/`M112`, fault recovery, heartbeat framing,
touchdown/settled hold/release, and buffered capture transfer. It is simulation,
not physical validation.

Build the firmware with:

```sh
/Users/hrmny/.platformio/penv/bin/pio run -e production
```
