# Define the simple unloaded-motor powered bring-up and smoke test

Type: grilling
Status: resolved
Blocked by: 02, 03

## Question

What is the smallest useful powered test for standalone Motor 1 that proves the
phase/Hall mapping and basic closed-loop motion without turning replaceable
driver or motor protection into a long commissioning project?

## Answer

Use one fixed, automated smoke test on the standalone unloaded Motor 1. Damage
to the Motor 1 bridge or motor is an accepted development risk; replacing one
is cheaper than building an elaborate qualification ladder. This acceptance
removes component-protection ceremony, but does not waive basic operator
safety.

### Minimum test boundary

- Test only Motor 1, with no gearbox, belt, suction box, or other load. Keep the
  motor secured, the shaft clear, and the Mean Well mains plug within reach.
  Change motor or phase wiring only with mains unplugged.
- Keep Motor 0 inactive for the whole firmware lifetime. Motor 1 must also stay
  inactive through boot, reset, upload, and serial connection; motion starts
  only after the script sends one explicit `RUN` command.
- Use fixed conservative firmware values rather than operator-tunable test
  parameters: a 0.9 V alignment voltage, a 2.0 V internal voltage clamp, a
  0.75 A motion limit, and a 1.0 A reported-current cutoff once current
  sensing is active. The values are smoke-test bounds, not production limits or
  calibrated ratings.
- Any detected firmware fault ends the sequence, commands zero PWM, and marks
  Motor 1 inactive in software. Stock SimpleFOC alignment is blocking, so this deliberately
  does not add the independent asynchronous trip path from the old ladder. The
  operator supervises alignment and pulls mains for unexpected sustained
  motion, violent vibration, smoke, odor, or rapid heating.

Do not require the NC bench-abort switch, phase-by-phase pulse gates, an AN8009
comparison, PWM waveform/dead-time qualification, current-sense linearity
calibration, per-stage manual authorization, high-rate ESP-side capture, or a
ten-round-trip repeatability run for this test.

### One automated smoke sequence

After a single operator confirmation that the unloaded motor is secured and
clear, the script sends `RUN`. Firmware then performs the following without
more prompts:

1. Confirm all Motor 0 and Motor 1 PWM commands are off, then run
   SimpleFOC Hall, phase, and current-sense alignment at the fixed low alignment
   voltage. A host-side deadline tells the operator to pull mains if the
   blocking call does not return promptly.
2. Probe Motor 1 once at a 0.75 A current limit and 2 rad/s: each commanded
   direction ends after at most 12 legal Hall edges or the existing 2.5-second
   timeout. During the intervening active stop, abort and command PWM off at a
   Hall reversal or 6 legal edges. Do not escalate beyond that automatically.
3. Command zero PWM, mark Motor 1 inactive in software, and print one terminal `PASS` or `FAIL`
   summary.

The basic automatic checks are deliberately coarse:

- alignment completes without a reset or SimpleFOC error;
- Hall states remain legal and the reported velocity sign changes between the
  two motion stages;
- the transcript identifies whether motion passed at the fixed 0.75 A step;
- current readings are finite and non-clipping, with no reported value above
  the configured 1.0 A threshold;
- the motor reports stopped and all PWM commands report off at the end or after
  any failure.

Exact speed/current tracking tolerances, phase-current accuracy, missed-sample
accounting, and repeatability are not gates. The operator records only whether
the motor turned smoothly in both directions and whether there was objectionable
noise, vibration, smell, smoke, or heat.

### Operator commands and evidence

Keep the entire test to two commands:

```sh
/Users/hrmny/.platformio/penv/bin/pio run -e motor-smoke -t upload
/Users/hrmny/.platformio/penv/bin/python tools/motor_smoke_test.py
```

The script should auto-detect the sole candidate serial port and accept
`--port <device>` when detection is ambiguous. It owns the one confirmation,
sends `RUN`, waits for the bounded sequence, writes a compact text transcript,
and exits zero only on `PASS`; a separate serial-monitor command is unnecessary.

The transcript needs only firmware identity, alignment result, Hall transition
counts and direction signs, the current step that produced motion, reported peak
current, approximate velocity in each direction, final PWM command state, fault/reset
reason, and the operator observation. This is enough to decide whether to
continue into control-architecture work or debug a conspicuous failure.

Passing establishes only that this particular unloaded Motor 1 setup can align,
turn both ways, stop, and produce plausible Hall/current feedback. It does not
qualify current accuracy, thermal endurance, hardware protection, the assembled
lift axis, production limits, homing, touchdown, or production acceptance.

## Comments

- 2026-08-01: Replaced the original ten-gate qualification ladder with a
  two-command smoke test after accepting the replaceable Motor 1 bridge and
  motor as development consumables.
- 2026-08-01: Corrected the hardware model after checking the V1.0 schematic.
  GPIO 21/22 are unconnected and there is no separate bridge-enable input;
  `disable` in this procedure means zero PWM plus inactive SimpleFOC state.
  Pulling mains remains the only bridge-power isolation.
- 2026-08-04: The operator approved a 1.0 V sensor-alignment voltage after
  clean Hall transitions but insufficient continuous motion at 0.5 V. This
  changes only alignment excitation; the 2.0 V internal clamp and the 0.25 A
  and 0.50 A powered-motion limits remain unchanged. Current limiting is not
  active during SimpleFOC sensor alignment.
- 2026-08-04: After 1.0 V produced successful alignment but a nominal 1.072 A
  peak, the operator approved reducing alignment to 0.9 V. The 1.0 A reported
  cutoff, 2.0 V internal clamp, and 0.25 A/0.50 A motion limits remain
  unchanged.
- 2026-08-04: After neither 0.25 A nor 0.50 A produced unloaded motion, the
  operator approved increasing only the retry step to 0.75 A. The 1.0 A
  filtered d/q cutoff and 2.0 V internal clamp remain unchanged.
- 2026-08-04: The operator then approved removing the now-redundant 0.25 A
  attempt. The current procedure performs one 0.75 A bidirectional probe with
  no automatic retry; the 1.0 A filtered d/q cutoff and every voltage, timing,
  telemetry, and stopping bound remain unchanged.
- 2026-08-04: After the first 0.75 A run oversped and moved substantially during
  stopping, the operator approved edge-bounding the diagnostic. Each motion
  direction is now capped at 12 legal Hall edges and each active stop at 6;
  stop reversal or edge-limit detection terminates the run and commands PWM
  off. Signed stop current-setpoint, q-current, q-voltage, Hall direction, and
  reversal telemetry were added without raising any electrical limit.
