# Implement and run the simple unloaded-motor smoke test

Type: task
Blocked by: 04

## Question

Implement and run the two-command smoke test defined by [Define the simple
unloaded-motor powered bring-up and smoke
test](04-define-safe-powered-bring-up-and-motor-test-ladder.md). Does the
standalone Motor 1 align, turn slowly in both directions, stop, and finish with
the bridge disabled while its Hall and current feedback remain plausible?

### Implementation scope

- Add a `motor-smoke` PlatformIO environment and
  `src/motor_smoke/main.cpp` without changing the validated `hall-validation`
  and `simplefoc-hall-validation` environments.
- Add fixed-purpose firmware that holds Motor 0 inactive, holds Motor 1 inactive
  until an explicit `RUN`, and automatically executes low-voltage alignment,
  a 0.25 A bidirectional motion probe, at most one fixed 0.50 A retry when the
  probe produces no Hall motion and no fault, and final disable.
- Keep all current, voltage, speed, and duration values compiled into the test.
  Expose no interactive tuning commands and run no stage indefinitely.
- Add `tools/motor_smoke_test.py`, using the PlatformIO Python environment's
  serial support. It auto-detects a sole serial candidate, permits `--port` when
  needed, asks for one operator confirmation, sends `RUN`, saves the text
  transcript, and exits nonzero on `FAIL`, reset, lost serial, or a final bridge
  state other than disabled. If its host deadline expires during blocking
  alignment, it must tell the operator to pull mains rather than pretend that
  closing serial disabled the bridge.
- Print compact stage results sufficient to check alignment, legal Hall
  transitions, opposite velocity signs, finite/non-clipping current below the
  1.0 A check once current sensing is active, which current step produced
  motion, and final stop/disable. Use fixed 0.5 V alignment and 2.0 V internal
  clamps. Do not build the previously proposed high-rate capture system,
  multimeter workflow, manual gate ladder, precision controller
  characterization, or repeatability loop.

### Operator procedure

With the unloaded motor secured, the shaft clear, the mains plug reachable, and
the phase wiring changed only while unplugged, run exactly:

```sh
/Users/hrmny/.platformio/penv/bin/pio run -e motor-smoke -t upload
/Users/hrmny/.platformio/penv/bin/python tools/motor_smoke_test.py
```

If more than one serial device is present, add `--port <device>` to the second
command; that remains the same step. The operator pulls mains for unexpected
sustained motion, violent vibration, smoke, odor, or rapid heating. Driver or
motor damage is an accepted test outcome, not a reason to add more prerequisite
gates.

### Evidence to record when resolving this task

Record the build result separately from the physical result. Attach or quote the
script transcript and add the operator's smooth-motion/noise/vibration/odor/heat
observation. Report the actual alignment outcome, Hall transition counts and
direction signs, whether motion passed at 0.25 A or required the 0.50 A retry,
reported peak current, approximate velocity in each direction, fault/reset
reason, and final bridge state.

A pass supplies coarse powered evidence for the next control-architecture
decision. It must not be described as current calibration, thermal validation,
assembled lift-axis validation, or production acceptance. A failure supplies a
small reproducible case for `diagnosing-bugs`; do not expand this task into a
qualification campaign.

## Comments

- 2026-08-01: Scope reduced from the original ten-gate powered ladder to one
  automated smoke sequence and two operator commands. Component loss is an
  accepted development risk; basic operator precautions remain.
