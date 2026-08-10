# Hall validation

These diagnostics validate only the three Hall signals from the StepperOnline
42BSA78-24-01 motor. Neither environment initializes PWM, a motor object, or a
motor driver:

- `hall-validation` runs the independent raw GPIO validator.
- `simplefoc-hall-validation` runs the same validator while SimpleFOC 2.4.0
  independently decodes every Hall interrupt as shaft angle and velocity.

## Wiring and safety

- Hall A: MKS `SDA_1`, GPIO 23
- Hall B: MKS `SCL_1`, GPIO 5
- Hall C: MKS `I_1`, GPIO 13
- Hall sensor supply: 5 V
- Hall signal pull-ups: external 3.3 V pull-up board only
- Motor 0 phase controls A/B/C: GPIO 32/33/25, held low by the diagnostic
- Motor 1 phase controls A/B/C: GPIO 26/27/14, held low by the diagnostic

The MKS ESP32 FOC V1.0 schematic has no separate motor-driver enable input;
GPIO 21 and GPIO 22 are unconnected. Holding all six phase controls low means
PWM is off, but it does not remove bridge power. Only disconnecting motor power
provides physical isolation.

Keep the motor phase leads disconnected for this test. The SS41F/SS41G Hall
sensors have open-collector sinking outputs; the external pull-ups establish
ESP32-safe logic-high levels.

## Build

The default environment remains the raw-only validator. Build both variants
before flashing the SimpleFOC test:

```sh
/Users/hrmny/.platformio/penv/bin/pio run -e hall-validation
/Users/hrmny/.platformio/penv/bin/pio run -e simplefoc-hall-validation
```

Both environments use 500000 baud. The firmware uses the shared
`kSerialBaud` constant, and PlatformIO applies the same monitor rate to every
environment through the default `[env]` settings in `platformio.ini`. Changing
only the monitor rate will not work.

## Flash and monitor

Flash the SimpleFOC variant explicitly:

```sh
/Users/hrmny/.platformio/penv/bin/pio run -e simplefoc-hall-validation -t upload
/Users/hrmny/.platformio/penv/bin/pio device monitor -e simplefoc-hall-validation
```

The expected boot record begins with
`READY app=simplefoc-hall-validation baud=500000` and confirms
`motor0_pwm=LOW motor1_pwm=LOW pwm_state=OFF`, `simplefoc=2.4.0`,
`pole_pairs=4`, and
`pullups=EXTERNAL`.

## Live check

After boot, the monitor prints `READY`, followed by an `ALIVE` heartbeat every
500 ms. Turning the shaft prints one `LIVE` record for every observed Hall
edge. Each `abc` field is ordered as the physical Hall A/B/C wires. In the
SimpleFOC environment, the same line also includes its decoded mechanical step,
angle, capture-relative angle, and velocity.

Commands are:

- `R`: start a guided one-revolution capture;
- `S` or `?`: print current status or the latest summary;
- `D`: in the SimpleFOC environment, toggle a compact 20 Hz angle/velocity
  dump. The dump is off by default.

## Guided revolution check

Mark the directly accessible motor shaft, then send `R`. Turn the shaft in one
direction until the validator stops automatically after 24 valid transitions.
For this 8-pole motor, the shaft mark should have completed one revolution.

A signal-level pass requires:

- all six legal Hall states were observed;
- `000` and `111` were never observed;
- exactly one signal changed at every edge;
- the six-state sequence stayed in one direction;
- no interrupt records were duplicated or dropped;
- state after 24 transitions matched the starting state.

The SimpleFOC environment additionally requires:

- SimpleFOC observed exactly 24 interrupts and 24 one-step changes;
- every decoded change was exactly one 15-degree mechanical Hall step;
- all steps used one sign, with no zero, skipped, or reversed steps;
- the absolute capture-relative angle was within half a Hall step (7.5
  degrees) of `2*pi`.

The top-level `RESULT status=PASS` requires both the raw and SimpleFOC checks to
pass. The following `SIMPLEFOC` record reports its independent status and uses
`POSITIVE` or `NEGATIVE` only; these signs do not claim physical clockwise or
counter-clockwise rotation. Velocity is diagnostic during hand rotation and
does not affect pass/fail.

The firmware reports `FORWARD_SEQUENCE` or `REVERSE_SEQUENCE`; these names do
not claim a physical clockwise direction. A separate `SEQUENCE` record prints
the six observed states and the corresponding reverse cycle without remapping
the physical Hall labels. Confirm the shaft mark manually when the `CHECK
shaft_mark_returned=REQUIRED` line appears. Send `S` to print the latest summary
again. Repeat the guided capture in the opposite direction. Both captures must
pass, their SimpleFOC signs must be opposite, and the shaft mark must return
after each capture.

## Validated hardware results — 2026-07-31

The sensor-only procedure passed on an MKS ESP32 FOC V1.0 with the
42BSA78-24-01 motor, the A/B/C mapping documented above, and the external 3.3 V
pull-up board. The Hall behavior passed on that hardware; rerun the procedure
after flashing the current 500000-baud firmware to validate the updated serial
transport on device.

| Sequence direction | Observed states | SimpleFOC angle delta | Duration | Result |
|---|---|---:|---:|---|
| `FORWARD_SEQUENCE` | `101-100-110-010-011-001` | `+6.283185 rad` | `14.759507 s` | `PASS` |
| `REVERSE_SEQUENCE` | `001-011-010-110-100-101` | `-6.283185 rad` | `7.593397 s` | `PASS` |

Each successful capture reported 24 raw edges, 24 SimpleFOC interrupts, 24
one-step changes, all six legal Hall states, and zero invalid states, invalid
transitions, duplicates, reversals, or dropped events. The shaft mark returned
after one motor revolution in both directions. SimpleFOC reported opposite
angle signs for the two sequence directions.

The operator identified the first direction as clockwise and the second as
counter-clockwise, but the shaft viewing side was not recorded. Firmware and
documentation therefore continue to use sequence direction and angle sign
rather than claiming an absolute physical CW/CCW mapping.

This validates the Hall inputs and SimpleFOC's `HallSensor` interpretation
only. The motor phases were disconnected and the firmware did not initialize
PWM or a motor driver. Earlier versions also drove GPIO 21/22 low, but the
schematic marks those pins unconnected; that action did not disable either
bridge. PWM, phase alignment, motor actuation, and controlled-speed velocity
accuracy were not tested.

## Known SimpleFOC velocity risk

SimpleFOC is pinned exactly to version 2.4.0 for repeatable validation. An
upstream Hall velocity issue can report stale nonzero velocity after the
32-bit microsecond timer wraps, approximately 40 minutes after a qualifying
transition. This short angle-validation milestone does not resolve that issue.
Long-idle soak testing or an upstream/local fix is required before Hall-derived
velocity is trusted in long-running closed-loop control. See
<https://github.com/simplefoc/Arduino-FOC/pull/469>.
