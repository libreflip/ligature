# Implement and run the simple unloaded-motor smoke test

Type: task
Status: resolved
Blocked by: 04

## Question

Implement and run the two-command smoke test defined by [Define the simple
unloaded-motor powered bring-up and smoke
test](04-define-safe-powered-bring-up-and-motor-test-ladder.md). Does the
standalone Motor 1 align, turn slowly in both directions, stop, and finish with
all PWM commands off while its Hall and current feedback remain plausible?

### Implementation scope

- Add a `motor-smoke` PlatformIO environment and
  `src/motor_smoke/main.cpp` without changing the validated `hall-validation`
  and `simplefoc-hall-validation` environments.
- Add fixed-purpose firmware that holds Motor 0 inactive, holds Motor 1 inactive
  until an explicit `RUN`, and automatically executes low-voltage alignment,
  one fixed 0.9 V voltage-mode open-loop revolution in each direction, and a
  final zero-PWM state. Ramp voltage to 0.9 V over 50 ms, ramp each direction
  to 10 motor-rad/s over 500 ms, and stop after 24 legal Hall edges.
- Keep all current, voltage, speed, and duration values compiled into the test.
  Expose no interactive tuning commands and run no stage indefinitely.
- Add `tools/motor_smoke_test.py`, using the PlatformIO Python environment's
  serial support. It auto-detects a sole serial candidate, permits `--port` when
  needed, asks for one operator confirmation, sends `RUN`, saves the text
  transcript, and exits nonzero on `FAIL`, reset, lost serial, or a final PWM
  state other than off. If its host deadline expires during blocking
  alignment, it must tell the operator to pull mains rather than pretend that
  closing serial forced PWM off or isolated bridge power.
- Print compact stage results sufficient to check alignment, legal Hall
  transitions, opposite velocity signs, finite/non-clipping monitored current
  below the 2.0 A filtered d/q gross-fault check, the fixed motion voltage, and final
  stopped/zero-PWM state. Stop by commanding PWM off and passively observing
  Hall quiet time; do not energize a zero-speed holding field. Use fixed 0.9 V
  alignment and motion voltage with a 2.0 V driver clamp. Do not build the
  previously proposed high-rate capture system,
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
direction signs, whether motion passed at the fixed 0.9 V motion voltage,
reported peak current, approximate velocity in each direction, fault/reset
reason, and final PWM command state.

A pass supplies coarse powered evidence for the next control-architecture
decision. It must not be described as current calibration, thermal validation,
assembled lift-axis validation, or production acceptance. A failure supplies a
small reproducible case for `diagnosing-bugs`; do not expand this task into a
qualification campaign.

## Answer

Yes, within the deliberately coarse unloaded smoke-test scope. Motor 1 aligned,
completed one net Hall-observed revolution in each commanded direction, coasted
to Hall quiet after each direction, and finished with all PWM commands off.
The final run recorded opposite Hall sequence signs, no invalid Hall states or
transitions, no reset, edge-derived speeds of -7.032 and +6.786 rad/s, and a
nominal peak current of 1.571 A below the approved 2.0 A gross-fault bound.

The operator described the motion as modestly rough. That remains commissioning
work and may be investigated later with the bounded development-only SimpleFOC
GUI path; it does not invalidate this basic bridge/motor/Hall smoke result. No
smoke, odor, or heating observation was supplied for the final run. This result
does not calibrate current, qualify closed-loop control, validate the assembled
lift axis, or constitute production acceptance.

## Comments

- 2026-08-01: Scope reduced from the original ten-gate powered ladder to one
  automated smoke sequence and two operator commands. Component loss is an
  accepted development risk; basic operator precautions remain.
- 2026-08-01: Implemented the fixed `motor-smoke` firmware and supervised host
  runner. `motor-smoke`, `hall-validation`, and
  `simplefoc-hall-validation` all compile successfully; the runner CLI and its
  terminal PASS/FAIL parsing were also checked. No ESP32 USB/UART serial device
  was attached, so upload and physical motion were not attempted. This ticket
  remains claimed pending the documented two-command device test and operator
  observation.
- 2026-08-01: Corrected the implementation after checking the V1.0 schematic:
  GPIO 21/22 are unconnected and there are no bridge-enable pins. The firmware
  now initializes Motor 1's three-argument driver, holds Motor 0 phase controls
  low, and reports `pwm_state=OFF`; the runner no longer equates software
  disable with bridge-power isolation. Pulling mains is the only physical
  isolation available during a blocked or unreachable run.
- 2026-08-01: After the schematic correction, `motor-smoke`,
  `hall-validation`, and `simplefoc-hall-validation` all compile successfully.
  The schematic-contract regression test passes, the runner accepts the new
  zero-PWM result and rejects the obsolete bridge-disabled result, and no
  USB/UART board is attached. Upload and physical motion remain pending, so the
  task remains claimed.
- 2026-08-04: The first operator-run alignment returned `result=0`, zero Hall
  edges, and zero current samples, then safely reported all PWM commands off.
  The transcript is retained in
  `evidence/motor-smoke-alignment-failure-20260804.txt`. Source tracing shows
  SimpleFOC failed sensor alignment before reaching current-sense alignment;
  the firmware incorrectly masked that first failure as
  `ALIGNMENT_CURRENT_IMPLAUSIBLE`. A host-tested classifier now reports this
  case as `SENSOR_ALIGNMENT_NO_HALL_MOTION` and adds start/end Hall states,
  current-stage/sample count, and motor status. The corrected `motor-smoke`
  environment compiles locally. Alignment voltage and the physical test
  sequence are unchanged; only the operator will upload or command the ESP.
- 2026-08-04: The operator reran the corrected firmware. During the 3.2-second
  alignment window it recorded 11,681 Hall interrupt callbacks, 11,677 invalid
  transitions, no invalid steady states, and unchanged start/end state `001`.
  Current-sense alignment was not reached, and the terminal record again
  reported all PWM commands off. The retained transcript is
  `evidence/motor-smoke-20260804T193148Z.txt`. Because SimpleFOC discards
  unchanged-state callbacks and the earlier phase-disconnected Hall test had
  zero duplicates, this is evidence of a powered Hall interrupt/glitch storm,
  not plausible rotor rotation. The initially proposed run with the 24 V motor
  supply unplugged was invalid because this board's ESP32 is not USB-powered;
  that test cannot execute. Do not suppress duplicate callbacks or raise
  alignment voltage before powered and non-PWM baselines are compared.
- 2026-08-04: The operator observed approximately 20–25 degrees of rotor
  movement during the powered alignment. This rules out a completely inactive
  phase-power path and is broadly consistent with the four non-invalid Hall
  transitions in the trace, but it is far short of the approximately 90
  mechanical degrees represented by one electrical revolution on this
  four-pole-pair motor. SimpleFOC sampled the same `001` Hall sector at the
  alignment boundaries and returned failure before current-sense alignment.
  Do not raise the 0.5 V alignment setting while the concurrent 11,677-callback
  Hall glitch storm remains unexplained; first repeat the stationary Hall
  validator with the board powered normally and PWM inactive.
- 2026-08-04: With 24 V connected and the `hall-validation` firmware holding
  PWM inactive, the operator observed a stationary `001` state and zero live
  edges or dropped events continuously from 4.0 through 25.0 seconds. This
  falsifies an always-noisy Hall input and localizes the storm to conditions
  introduced by active PWM/power-stage operation. The next one-variable split
  is `motor-smoke` with Motor 1 U/V/W physically disconnected while power is
  off, then 24 V restored for the operator-run test: a persistent storm points
  to PWM/gate-drive coupling or a pin conflict; a quiet Hall trace points to
  phase-current/cable coupling or motor-power ground disturbance.
- 2026-08-04: The operator clarified that the powered alignment motion was
  smooth, approximately 20–25 degrees per excursion, and repeated about three
  times back and forth. This further rules out a dead bridge and makes gross
  phase-path failure less likely. It does not explain the 11,677 invalid Hall
  callbacks: only four callbacks represented legal transitions, which is the
  expected order of magnitude for such a small smooth excursion. The planned
  phase-disconnected PWM differential remains necessary before changing the
  alignment voltage or filtering Hall callbacks in software.
- 2026-08-04: With Motor 1 U/V/W disconnected and the same `motor-smoke`
  firmware generating PWM commands, the operator recorded zero Hall edges,
  zero invalid states, and zero invalid transitions throughout alignment; the
  transcript is `evidence/motor-smoke-20260804T194010Z.txt`. The expected
  terminal failure was `SENSOR_ALIGNMENT_NO_HALL_MOTION`, current-sense
  alignment was not reached, and all PWM commands ended off. This falsifies an
  ESP interrupt bug, PWM-pin conflict, and on-board gate-switching noise as the
  primary cause. The 11,677-callback storm requires the external motor phase
  path: phase-current cable coupling into the Hall bundle and motor/power-ground
  disturbance are now the leading causes. Correct physical signal integrity
  before retrying powered alignment; do not hide the storm by filtering
  duplicate callbacks in firmware.
- 2026-08-04: After twisting the phase conductors and separating them from the
  Hall wiring, the operator reconnected U/V/W and reran the 0.5 V alignment.
  The prior glitch storm was gone: four Hall edges were all legal, with zero
  invalid states or transitions. The rotor twitched back and forth rather than
  following a continuous sweep, returned to `001`, and SimpleFOC reported
  `SENSOR_ALIGNMENT_FAILED` before current-sense alignment. The transcript is
  `evidence/motor-smoke-20260804T201224Z.txt`. This confirms the wiring layout
  caused the interrupt storm and leaves insufficient alignment torque or a
  weak/missing phase output as the remaining hypotheses. A proposed next step
  is a single modest increase from 0.5 V to 0.75 V; because current limiting is
  not active during sensor alignment, that electrical-bound change requires
  explicit operator approval before implementation or another powered run.
- 2026-08-04: The operator chose and approved 1.0 V rather than 0.75 V or the
  initially suggested 2.0 V for the next sensor-alignment attempt. Only the
  alignment voltage changes: the 2.0 V internal clamp and 0.25 A/0.50 A motion
  limits remain unchanged. The assistant may build locally but must not upload,
  open the serial port, or send `RUN`; all ESP-facing actions remain with the
  operator.
- 2026-08-04: At 1.0 V, the operator's run completed sensor and current-sense
  alignment: `result=1`, `motor_status=4`, 13 legal Hall edges, zero invalid
  states/transitions, and `current_stage=PASS`. The firmware then deliberately
  stopped before either motion probe because its provisional 0.01-ohm/20-V/V
  scale reported a 2.353 A alignment peak, above the fixed 1.0 A plausibility
  cutoff. Evidence is `evidence/motor-smoke-20260804T202017Z.txt`. This proves
  that 1.0 V is sufficient for alignment and that phase/current-sense alignment
  is accepted; it does not prove 2.353 physical amperes because absolute current
  scale remains uncalibrated. Do not weaken the cutoff or begin closed-loop
  current motion until the reported-current scale is resolved against the
  populated hardware or an independent current measurement.
- 2026-08-04: The operator confirmed `1AED` markings on both populated Motor 1
  current amplifiers. TI identifies this as INA181A2, 50 V/V. The smoke-test
  firmware's previous 20 V/V assumption therefore over-reported current by
  2.5x; the prior 2.353 A peak rescales nominally to 0.941 A. Correct the fixed
  gain to 50 V/V and retain the 1.0 A cutoff. This is sufficient for the next
  bounded smoke-test attempt but remains nominal rather than calibrated current
  metrology.
- 2026-08-04: With the corrected 50 V/V gain and 1.0 V alignment, the operator
  observed a smooth approximately 90-degree sweep back and forth. For the
  four-pole-pair motor, 90 mechanical degrees is one electrical revolution and
  is coherent with sensor alignment. Firmware recorded `result=1`,
  `motor_status=4`, 14 legal Hall edges, zero invalid states/transitions, and
  passing current-sense alignment. It stopped before the 0.25 A motion probe
  because the nominal peak was 1.072 A, 0.072 A above the unchanged 1.0 A
  cutoff. Evidence is `evidence/motor-smoke-20260804T203214Z.txt`. Preserve the
  cutoff; a reduction to 0.9 V alignment is the next proposed one-variable test
  because a proportional estimate places its peak near 0.965 A while the 1.0 V
  run demonstrated robust alignment margin. This electrical-bound change still
  requires explicit operator approval.
- 2026-08-04: The operator approved the proposed 0.9 V alignment setting. Keep
  the 1.0 A reported-current cutoff, 2.0 V internal clamp, 50 V/V Motor 1 gain,
  and 0.25 A/0.50 A motion limits unchanged. The assistant remains restricted
  to local edits, tests, and compilation; the operator retains all ESP-facing
  actions.
- 2026-08-04: At 0.9 V, alignment passed with 13 legal Hall edges, no invalid
  states/transitions, and a nominal 0.905 A peak. The operator observed no
  movement after alignment. Both 0.25 A directions completed with zero Hall
  edges and nominal raw-or-d/q peaks of 0.610 A and 0.579 A. The 0.50 A retry
  then stopped immediately at the unchanged safety gate on a 1.048 A peak,
  before visible movement. The runner transcript path was not included in the
  pasted output; this operator-provided terminal output is the current record.
  This establishes successful alignment but not powered motion. The current implementation
  reports and gates on the maximum of unfiltered raw phase samples and filtered
  d/q current, so the next diagnostic should separately report raw-phase peak,
  filtered d/q peak, voltage peak, and current setpoint without changing any
  electrical bound.
- 2026-08-04: The operator approved the telemetry-only diagnostic. Motion
  outcomes now report `raw_phase_peak_a`, `dq_peak_a`, `current_sp_peak_a`, and
  `dq_voltage_peak_v` separately while retaining the composite safety peak and
  every existing cutoff, alignment voltage, motion limit, duration, and stop
  behavior. The assistant remains restricted to local preparation and build;
  the operator retains upload and `RUN`.
- 2026-08-04: Split telemetry showed that both 0.25 A directions reached their
  full setpoint but produced no Hall edges: positive reported raw/d-q/voltage
  peaks of 0.596 A, 0.263 A, and 0.281 V; negative reported 0.581 A, 0.340 A,
  and 0.285 V. The 0.50 A retry stopped after about 6 ms while its setpoint was
  only 0.104 A: a raw phase sample reached 1.065 A, but filtered d/q current was
  0.212 A. This distinguishes insufficient 0.25 A breakaway torque from the
  retry failure, which is caused by applying the 1.0 A gate to an isolated raw
  phase peak rather than the filtered current used by the controller. A
  proposed next step is to retain raw-peak telemetry but apply the unchanged
  1.0 A motion cutoff to filtered d/q current, allowing the already-approved
  0.50 A retry to execute. Because that changes safety-gate semantics, it
  requires explicit operator approval.
- 2026-08-04: The operator approved the motion-gate semantic change. During
  motion and stopping, the unchanged 1.0 A cutoff now evaluates the existing
  2 ms-filtered d/q peak used by the current controller; raw phase peaks remain
  visible in every motion outcome but do not independently trip the gate.
  Alignment retains its existing raw-current plausibility check. No voltage,
  current, alignment, duration, retry, or stopping bound changed, and the
  operator retains all ESP-facing actions.
- 2026-08-04: With the filtered d/q gate active, 0.25 A and 0.50 A completed in
  both directions without a cutoff or Hall fault but produced no visible motion
  and zero Hall edges. At 0.50 A the positive direction reached 0.646 A filtered
  d/q and 1.838 V, while negative reached 0.513 A and 0.459 V; the full 0.500 A
  setpoint was reached in each direction. Raw phase peaks were 1.536 A and
  0.763 A, and the final zero-PWM/stopped checks passed. This proves the lack of
  motion is not caused by the velocity loop failing to request 0.50 A. Because
  the successful 0.9 V alignment reported about 0.8 A and visibly moved the
  rotor, a proposed next one-variable test is a 0.75 A retry while retaining the
  1.0 A filtered d/q cutoff and 2.0 V clamp. This raises the approved motion
  current bound and requires explicit operator approval.
- 2026-08-04: The operator approved changing only the no-motion retry from
  0.50 A to 0.75 A. Retain the initial 0.25 A step, 1.0 A filtered d/q cutoff,
  2.0 V clamp, 0.9 V alignment, durations, stop behavior, and split telemetry.
  If 0.75 A still produces no motion, do not increase current again; inspect
  electrical-angle and phase/current alignment using the directional telemetry.
- 2026-08-04: The operator approved removing the initial 0.25 A step because
  repeated 0.25 A and 0.50 A runs already established their no-motion outcome.
  The current smoke test now performs one 0.75 A bidirectional probe with no
  retry. The 1.0 A filtered d/q cutoff, 2.0 V clamp, 0.9 V alignment, durations,
  stop behavior, and split telemetry remain unchanged.
- 2026-08-04: The operator added that the successful 0.9 V alignment sweep
  itself showed some cogging rather than perfectly smooth motion. Mild stepping
  can be consistent with low-excitation alignment and motor detent torque, but
  together with zero 0.50 A closed-loop motion it increases suspicion of an
  electrical-angle or phase/current-association problem. Treat the single
  0.75 A probe as the final current escalation: if rotation is absent or not
  clean, return to alignment/phase diagnosis rather than increasing current.
- 2026-08-04: The single 0.75 A probe produced powered rotation but failed
  control and stopping acceptance. The operator observed a slightly unsmooth
  alignment, an initial approximately 10-degree movement, then roughly two
  rotations in each direction with an unsmooth sound. Firmware recorded 77
  legal edges during the positive stage and 54 more during the attempted stop,
  with no invalid Hall transitions. At the previously validated 24 edges per
  shaft revolution, those counts correspond to about 3.2 revolutions during
  motion and 2.25 revolutions during stopping. The reported velocity averaged
  24.175 rad/s and peaked at 33.636 rad/s against a 2.0 rad/s target; filtered
  d/q current remained below cutoff at 0.757 A and d/q voltage reached the 2.0 V
  clamp. The stop failed after 1.2 s at -28.447 rad/s, so the negative command
  stage never began; final PWM was nevertheless commanded off. Do not repeat or
  increase current. The next diagnosis must resolve signed velocity/current/
  voltage behavior, the large speed/count discrepancy, braking dynamics, and
  electrical-angle/phase association before another powered-motion run.
- 2026-08-04: The operator clarified that they saw a couple of rotations, a
  stop, and then another couple of rotations, but could not determine whether
  the second episode reversed direction. Preserve that uncertainty. The log
  establishes only that the second episode occurred while firmware was in
  `stop after=POSITIVE`; the negative command stage was never entered. Add
  explicit Hall direction/reversal and signed q-current/setpoint/voltage data
  for stopping before assigning a physical direction or changing controller
  tuning.
- 2026-08-04: The operator approved a bounded follow-up diagnostic. Each motion
  direction now ends at 12 legal Hall edges (half a shaft revolution at the
  validated 24 edges/revolution) or the existing 2.5-second timeout. Active
  stopping fails and commands PWM off at 6 legal edges (one quarter revolution)
  or any detected Hall-direction reversal. Stop outcomes now report Hall
  direction/reversals and signed minimum/maximum current setpoint, q current,
  and q voltage. The 0.75 A current limit, 1.0 A filtered d/q cutoff, 2.0 V
  clamp, and 0.9 V alignment remain unchanged.
- 2026-08-04: The bounded run reached exactly 12 legal edges in 0.766 s and then
  entered stopping. Motion still oversped: reported average/peak velocity was
  21.557/32.395 rad/s against a 2.0 rad/s target, q/d voltage reached the 2.0 V
  clamp, and filtered d/q current remained 0.618 A during motion. Six
  milliseconds into stopping, before any additional Hall edge, the 1.0 A
  filtered d/q gate tripped at 1.201 A. Signed stop telemetry showed the
  velocity controller still requesting +0.564 A maximum current and q voltage
  still reaching +2.0 V while q current ranged down to -0.298 A at about 30
  rad/s. The composite filtered peak therefore came from the d axis. Final PWM
  was off; evidence is `evidence/motor-smoke-20260804T205620Z.txt`. The edge
  bounds contained the prior runaway, but the fixed 1.0 A/s velocity-controller
  output ramp cannot reverse its current request on the observed acceleration
  timescale. Do not alter current polarity or increase current from this result:
  negative q current can be regenerative once back-EMF exceeds the clamped q
  voltage. The next proposed one-variable controller test is a faster bounded
  velocity-output slew while retaining all current, voltage, edge, and stop
  gates.
- 2026-08-04: The operator approved changing only
  `PID_velocity.output_ramp` from 1 A/s to 10 A/s so the velocity loop can remove
  or reverse its current request on a roughly 50–150 ms timescale. Retain the
  0.75 A current limit, 1.0 A filtered d/q cutoff, 2.0 V clamp, 0.9 V alignment,
  12-edge motion cap, 6-edge/reversal stop abort, durations, gains, and
  telemetry. The assistant remains restricted to local preparation and build.
- 2026-08-04: With the 10 A/s velocity-output slew, the bounded positive stage
  again reached 12 legal edges, but reported average velocity improved from
  21.557 to 1.626 rad/s against the 2.0 rad/s target; peak velocity remained
  11.305 rad/s. Motion d/q voltage stayed below clamp at 1.687 V and filtered
  d/q current peaked at 0.663 A. Stopping still failed after 6 ms and zero
  additional Hall edges: filtered d/q reached 1.196 A while q current ranged
  only to -0.279 A, so the transient was predominantly d-axis. The stop stage
  inherited a +0.530 A current request and +2.0 V q-controller output from the
  preceding motion stage. Evidence is
  `evidence/motor-smoke-20260804T210115Z.txt`; final PWM was off. SimpleFOC PID
  reset clears integral and previous-output state. The next proposed
  one-variable transition fix is to zero target/current state and reset the
  velocity plus q/d current PIDs before the first stop `loopFOC()` call, while
  retaining every numeric limit and edge gate.
- 2026-08-04: The operator added that the bounded rotation appeared to jump a
  substantial distance in roughly two steps rather than moving smoothly. This
  reinforces the peak-velocity and controller-state evidence; do not interpret
  the near-target average velocity as smooth regulation.
- 2026-08-04: The operator approved the stop-transition fix and flashing it.
  Before stop-stage observation or the first stop `loopFOC()` call, firmware now
  zeros `target` and `current_sp` and resets the velocity plus q/d current PIDs.
  No gain, slew, current, voltage, alignment, time, edge, fault, or telemetry
  setting changed. Upload is authorized, but the operator retains serial-script,
  `RUN`, and reset-for-capture control.
- 2026-08-04: The stop-reset image did not reach stopping. After successful
  alignment, the operator observed one fast movement. Firmware detected one
  skipped/illegal Hall transition after 10 total edges (9 legal) in 0.466 s and
  immediately commanded PWM off; evidence is
  `evidence/motor-smoke-20260804T210503Z.txt`. Nine legal transitions over that
  interval imply roughly 5 rad/s from the independently validated 24 edges per
  shaft revolution, consistent with the operator's observation that motion was
  faster than the 2 rad/s target. Filtered d/q current stayed at 0.634 A and
  d/q voltage at 1.744 V, below their gates. The stop-transition reset was not
  exercised. Repeated 0.50 A no-motion followed by 0.75 A breakaway, combined
  with 15 mechanical degrees per Hall transition, now identifies coarse
  Hall-velocity feedback plus breakaway/stick-slip as the leading cause of the
  jump behavior. Do not repeat the same closed-loop test. The next proposed
  diagnostic is current-limited 2 rad/s open-loop electrical rotation with Hall
  used only as an observer, the existing half-revolution/time/current/voltage
  gates, and immediate zero PWM at termination. This separates motor/current
  commutation smoothness from the coarse Hall velocity loop.
- 2026-08-04: The operator approved compiling and flashing the open-loop
  diagnostic. After unchanged sensor/current alignment, firmware switches only
  the probe to `velocity_openloop`, commands one positive 2 rad/s half-revolution
  at the existing 0.75 A/2.0 V limits, and keeps Hall capture solely as an
  observer/fault detector. At termination or fault it disables PWM before
  printing the outcome, then observes up to the existing 6-edge/1.2-second/
  400-ms-quiet coast bounds with PWM off. It does not re-energize for a negative
  direction because the post-coast electrical angle is not trusted. Terminal
  status remains `FAIL` with `OPEN_LOOP_DIAGNOSTIC_COMPLETE` even on a clean
  diagnostic so it cannot be mistaken for closed-loop smoke-test acceptance.
- 2026-08-04: The operator reported that open-loop motion was substantially
  smoother than closed-loop motion but still showed clearly visible cogging.
  Firmware recorded 9 legal Hall edges with no invalid state/transition over
  1.261 s before the unchanged 1.0 A filtered d/q gate stopped the run at
  1.005 A; raw phase peak was 1.896 A, current setpoint 0.750 A, and d/q voltage
  1.252 V. Evidence is `evidence/motor-smoke-20260804T211702Z.txt`; final PWM
  was off. Nine edges at 24 edges/revolution imply approximately 1.87 rad/s,
  close to the 2.0 rad/s open-loop command. This confirms motor/current
  commutation can track the intended low speed and isolates the severe jump/
  overspeed behavior to the closed-loop Hall velocity path. Because Hall did
  not control electrical angle or velocity in this run, the remaining cogging
  is intrinsic mechanical/commutation/current-loop behavior rather than Hall
  feedback oscillation. Do not weaken the 1.0 A gate for the 0.005 A overrun;
  separate q/d motion peaks and address low-speed Hall/controller architecture
  before returning to closed-loop acceptance.
- 2026-08-04: The operator clarified that the prior 2 rad/s value was arbitrary
  and approved a more informative motor-side open-loop profile. The diagnostic
  now ramps commanded velocity from 0 to 10 rad/s over 500 ms and permits at
  most 48 legal Hall edges (two motor revolutions) so it can reach a steadier
  operating point. The 0.75 A current limit, 1.0 A filtered d/q cutoff, 2.0 V
  clamp, 0.9 V alignment, illegal-Hall fail-fast, immediate PWM-off termination,
  and 6-edge/1.2-second/400-ms-quiet coast observation remain unchanged. This
  temporary diagnostic profile does not change closed-loop smoke-test
  acceptance.
- 2026-08-04: The 0-to-10 rad/s open-loop ramp stopped 451 ms after motion
  began when the filtered composite d/q peak reached 1.001 A. It recorded seven
  legal Hall edges with no invalid states or transitions, a 2.613 A nominal raw
  phase peak, the full 0.750 A current setpoint, and 1.678 V peak d/q voltage;
  final PWM commands were off. Seven of 24 edges/revolution over 0.451 seconds
  imply approximately 4.06 rad/s average physical velocity. The commanded
  linear ramp integrates to approximately 2.03 radians, or 7.75 expected Hall
  edges, over the same interval, so the rotor broadly tracked the ramp rather
  than grossly slipping or running away. The operator reported visibly and
  audibly rough motion with an almost scratchy sound. Evidence is
  `evidence/motor-smoke-20260804T212223Z.txt`. Do not raise the current cutoff or
  command a faster profile from this result. Existing telemetry combines d and
  q into one peak, so it cannot yet distinguish d-axis angle/commutation error
  from q-axis torque current; first discriminate mechanical roughness from
  powered commutation roughness with an unpowered hand-rotation observation.
- 2026-08-04: With mains unplugged, the operator felt only the motor's normal
  magnetic cogging and did not hear or feel the scratchy behavior from the
  powered run. This falsifies persistent bearing drag or rotor contact as the
  primary cause and localizes the roughness to energized commutation/current
  control. Firmware telemetry now separates q- and d-axis current peaks and
  records signed q/d current and voltage at the cutoff, commanded velocity, and
  elapsed time without changing the motion profile or any electrical bound.
  The `motor-smoke` environment compiles successfully. Upload did not occur:
  the ESP serial device was absent, PlatformIO incorrectly auto-selected
  `/dev/cu.Maestro`, and esptool received no serial data.

### Current diagnosis

No. The standalone Motor 1 does not yet satisfy the unloaded smoke-test
acceptance criteria.

Compilation and flashing succeeded separately from the physical result. On the
device, 0.9 V sensor alignment repeatedly passed with 12--15 legal Hall edges,
zero invalid states/transitions, `sensor_direction=CCW`, and nominal alignment
peaks of approximately 0.8--0.95 A. The operator saw the expected alignment
excursions, although with visible cogging. This establishes a functioning
bridge, motor phase path, Hall sequence, and repeatable SimpleFOC alignment; it
does not calibrate current.

The fixed 0.75 A closed-loop probe produced rotation but not controlled slow
motion. One run reached 77 legal edges with reported average/peak velocity of
24.175/33.636 rad/s against a 2 rad/s target, then recorded another 54 edges
during the attempted stop and failed `MOTOR_DID_NOT_STOP`. Later edge-bounded
runs still moved in large steps, reached 12 legal edges too quickly, and either
tripped during the stop transition or detected an illegal skipped Hall
transition. The negative commanded direction was never reached in a valid
bidirectional sequence, and physical stopping was not established. The
operator described the powered motion as unsmooth, cogged, and almost scratchy;
with mains removed, hand rotation produced only normal magnetic cogging and no
scratchiness. No odor or heating observation was reported.

A Hall-observed open-loop diagnostic isolated the severe jump/overspeed from
the motor's basic commutation path. At 2 rad/s it produced nine legal edges in
1.261 seconds, approximately 1.87 rad/s from the validated 24 edges/revolution,
but remained visibly cogged and stopped at the unchanged 1 A filtered d/q gate.
A 0-to-10 rad/s ramp repeated deterministically: seven legal edges in 445 ms
versus approximately 7.75 predicted, then q current exceeded the 0.75 A request
and reached the 1.000 A cutoff. At that fault, d current was 0.205 A, q/d
voltage was 1.028/-0.644 V, raw phase peak was a nominal 2.528 A, there were no
invalid Hall states/transitions or resets, and all Motor 0/Motor 1 PWM commands
ended off. Evidence is retained in
`evidence/motor-smoke-20260804T210115Z.txt`,
`evidence/motor-smoke-20260804T210503Z.txt`,
`evidence/motor-smoke-20260804T211702Z.txt`, and
`evidence/motor-smoke-20260804T214056Z.txt`.

The bounded failure supports the next architecture and commissioning decisions:
coarse 15-degree Hall velocity feedback is unsuitable for the attempted low-
speed loop as configured, and the provisional current PI gains/current scale
cannot yet be treated as regulated or calibrated torque. Do not raise current
or weaken the 1 A gate on this evidence. Current-loop characterization and the
choice of low-speed control strategy belong in the commissioning/control-
architecture work rather than further expansion of this smoke-test ticket.
- 2026-08-04: Primary-source follow-up is recorded in
  [`docs/research/simplefoc-hall-motor-parameters.md`](../../../docs/research/simplefoc-hall-motor-parameters.md).
  SimpleFOC's Hall examples confirm the API path but use generic, hardware-
  specific gains; upstream explicitly notes that 60-degree-electrical Hall
  quantization makes low-speed operation unsmooth. A 10:1 gearbox makes a
  higher motor-side test representative: 10 motor-rad/s produces about 38.2
  Hall updates/s and corresponds to only 1 output-rad/s. Do not run that closed-
  loop test yet because the open-loop diagnostic already reached the q-current
  cutoff at 8.9 rad/s. First measure the disconnected motor's three pairwise
  DCR and 1-kHz inductance values, then replace the arbitrary current PI/filter
  values with one explicit R/L- and loop-rate-derived bandwidth and repeat the
  bounded open-loop trajectory. The motor datasheet nominally specifies 1.03
  ohm/phase and 0.43 mH/phase at 1 kHz.
- 2026-08-05: With the motor disconnected, the operator measured identical
  U-V/V-W/W-U DCR values of 1.09 ohm, each plus/minus 0.01 ohm. At one rotor
  position, 1-kHz series results were respectively 429.4/433.0/514.2 uH and
  1.139/1.143/1.163 ohm. The balanced DCR and approximately 2-percent Rs spread
  argue against an open or resistively damaged phase. The W-U inductance is
  about 19 percent above U-V at that position, but all raw values fit the
  manufacturer's published tolerances if its nominal values are terminal-pair
  measurements. Preserve the raw readings: the three-terminal measurements do
  not identify internal wye/delta construction, and the datasheet's `per phase`
  convention is ambiguous. The next unpowered discriminator is pairwise Ls
  minimum/maximum over rotor position; saliency should move the high reading
  with rotor angle, while a phase asymmetry should remain tied to W-U.
- 2026-08-05: Six approximately even samples over 90 mechanical degrees gave
  U-V 426--525 uH, V-W 434--530 uH, and W-U 420--518 uH. The high value moved
  among all three terminal pairs with rotor position and all three ranges
  closely agree. This falsifies a W-U-specific inductance defect and identifies
  normal rotor-angle saliency. A terminal-equivalent wye controller model is
  approximately 0.545 ohm, 0.210 mH on the minimum-inductance axis, and 0.265
  mH on the maximum-inductance axis. These are provisional control-model values,
  not a claim about the inaccessible physical winding connection. The next
  one-variable powered diagnostic can replace the arbitrary current PI/filter
  values with one R/L-derived bandwidth, report the actual `loopFOC()` rate and
  derived gains, and repeat the unchanged bounded open-loop trajectory before
  any higher-speed closed-loop Hall test.
- 2026-08-05: Implemented the approved current-loop preparation without
  flashing. Firmware now loads the terminal-equivalent model (0.545 ohm,
  Ld=0.210 mH, Lq=0.265 mH), keeps q/d current gains at zero through alignment,
  and adds a zero-torque 256-cycle timing stage. It commands all PWM off and
  fails before motion if feedback is nonfinite, warm-up filtered d/q current
  reaches 1 A, or the measured complete `loopFOC()` plus `move()` rate is below
  2 kHz. On success it calls SimpleFOC 2.4.0
  `tuneCurrentController(100 Hz)`, resets both current PIDs, and reports loop
  period/frequency, derived q/d gains and filters, and warm-up peak current.
  The 0.75 A current limit, 1.0 A cutoff, 2.0 V clamp, open-loop speed ramp,
  edge cap, and time bounds are unchanged. `pio run -e motor-smoke` succeeds;
  RAM is 25,276/327,680 bytes (7.7 percent) and flash is
  396,400/1,310,720 bytes (30.2 percent). No upload, serial access, reset, or
  physical test was performed.
- 2026-08-05: The operator clarified that in the previously run image,
  alignment looked smoother than the subsequent powered movement. This is
  consistent with the leading diagnosis: SimpleFOC sensor alignment uses a
  prescribed voltage/electrical-angle sweep, while the motion diagnostic used
  the provisional measured-current PI loop. It therefore adds evidence that
  the roughness is introduced or amplified by current regulation rather than
  being solely mechanical cogging or a damaged winding. It does not validate
  the newly compiled R/L-derived current-loop changes, which remain unflashed
  and physically untested.
- 2026-08-06: At the operator's explicit request, the compiled R/L-derived
  current-loop image was flashed to `/dev/cu.usbserial-210`. PlatformIO rebuilt
  the `motor-smoke` environment, wrote bootloader/partitions/application,
  verified every written hash, and completed successfully before its normal RTS
  reset. RAM remained 25,276/327,680 bytes (7.7 percent) and flash remained
  396,400/1,310,720 bytes (30.2 percent). The assistant did not open serial,
  send `RUN`, or initiate the physical diagnostic. Device behavior remains
  untested after this flash.
- 2026-08-06: The operator ran the flashed diagnostic. Alignment passed with
  14 legal Hall edges, no invalid states/transitions, and 0.810 A peak reported
  phase current. The current-loop stage measured a 52 us complete loop period
  (19.23 kHz), comfortably above its 2 kHz minimum, and installed the 100 Hz
  model-derived gains: q P/I=0.166504/342.434 and d
  P/I=0.131947/342.434. Zero-torque warm-up peak filtered d/q current was
  0.186 A. Motion then reached the unchanged 1 A filtered d/q cutoff after
  only 159 ms, one legal Hall edge, and a commanded 3.16 rad/s: q was 1.018 A
  and d was -0.030 A at the fault, with a 0.75 A peak current request. This
  localizes the immediate trip to q-current overshoot rather than d-axis
  commutation error. SimpleFOC's current-mode `velocityOpenloop()` returns the
  full `current_limit` independently of target velocity, so the 500 ms velocity
  ramp still applies an immediate 0-to-0.75 A current step. The next proposed
  one-variable diagnostic is to retain the measured model, 100 Hz tuning, 1 A
  gate, 2 V clamp, velocity trajectory, edge cap, and time bounds while ramping
  the open-loop current request from 0 to 0.75 A at the existing closed-loop
  velocity controller's 10 A/s output-ramp rate. No firmware change or further
  hardware action has been authorized yet.
- 2026-08-06: The operator approved collapsing Issue 14 back to a coarse
  powered smoke test instead of continuing current-controller qualification.
  The compiled-only firmware now uses voltage torque with a 0.9 V motion
  command, ramps open-loop electrical rotation to 10 motor-rad/s over 500 ms,
  requires one shaft revolution (24 legal Hall edges) in each direction, and
  requires the observed Hall directions to be opposite. Current sensing remains
  monitoring-only with the unchanged 1.0 A filtered d/q cutoff; the driver
  clamp remains 2.0 V. Each motion stage immediately commands all PWM off, then
  observes 1.2 seconds of passive coast and requires at least 400 ms of Hall
  quiet before the next direction or terminal PASS. The measured-current PI,
  R/L tuning stage, energized `move(0)` stop, and current-mode open-loop step
  are no longer in the smoke-test path. `pio run -e motor-smoke` succeeds with
  25,260/327,680 bytes RAM (7.7 percent) and 392,144/1,310,720 bytes flash
  (29.9 percent); the host alignment-classifier test also passes. No upload,
  reset, serial access, or physical run was performed.
- 2026-08-06: With flashing pre-authorized by the operator, the simplified
  voltage-mode image was uploaded to `/dev/cu.usbserial-210`. Before upload,
  no smoke-test runner or PlatformIO monitor held the port. PlatformIO wrote
  the bootloader, partitions, boot application, and 392,544-byte firmware
  image; every written hash verified, and the uploader completed its normal
  RTS reset. The assistant did not open serial or send `RUN`. Physical motion
  behavior of this image remains operator-untested.
- 2026-08-06: The operator ran the first voltage-mode image; transcript
  `evidence/motor-smoke-20260806T194507Z.txt` records another clean alignment
  pass with 13 legal Hall edges, no invalid states/transitions, and 0.811 A
  nominal peak. The motion stage then applied the full 0.9 V field while the
  speed ramp was only at 0.12 rad/s and reached the unchanged cutoff after
  6 ms: filtered q/d peak was 1.004 A, raw phase peak was 0.996 A, and there
  were no Hall edges. PWM ended off with no reset. This falsifies current-PI
  instability as the immediate cause; SimpleFOC's voltage-mode
  `velocityOpenloop()` also returns the full voltage limit independently of
  target speed. The next compiled image ramps voltage and velocity together
  from zero to 0.9 V and 10 rad/s over the same 500 ms. All current, Hall,
  duration, and final-PWM gates remain unchanged. `pio run -e motor-smoke`
  succeeds with 25,260/327,680 bytes RAM (7.7 percent) and
  392,168/1,310,720 bytes flash (29.9 percent). This image has not yet been
  physically run.
- 2026-08-06: After confirming the prior runner had exited and the serial port
  was free, the pre-authorized synchronized voltage-ramp image was flashed to
  `/dev/cu.usbserial-210`. PlatformIO wrote and hash-verified all images and
  completed its normal RTS reset. The assistant did not open serial or send
  `RUN`; device behavior remains for the operator-run test.
- 2026-08-06: The synchronized voltage/velocity ramp did not produce a Hall
  transition. The operator reported the rotor barely moved. At 389 ms the
  command had reached 0.700 V and 7.78 rad/s, but Hall edges remained zero;
  filtered q current then reached the unchanged 1.000 A cutoff (raw phase peak
  1.200 A) and all PWM ended off. Evidence is
  `evidence/motor-smoke-20260806T194831Z.txt`. This falsifies the assumption
  that the rotor would follow the simultaneous ramp and generate the estimated
  speed-proportional back EMF: the rotating field outran the stalled rotor.
  SimpleFOC does synchronize `shaft_angle` from the Hall sensor after alignment,
  so a stale open-loop starting angle is not the leading explanation. The
  remaining bound is explicit: this 4.80 A-rated motor's measured 0.545-ohm
  phase-equivalent resistance predicts approximately 1.65 A at the fixed
  0.9 V motion voltage, while the smoke test stops at 1.0 A before it breaks
  through cogging. Raising that software threshold is a material electrical-
  bound decision and has not yet been authorized.
- 2026-08-06: The operator approved raising the filtered d/q gross-fault cutoff
  from 1.0 A to 2.0 A and giving voltage a faster ramp than speed. The next
  image ramps voltage from zero to 0.9 V over 50 ms while retaining the 500 ms
  ramp to 10 rad/s. At 50 ms the commanded shaft angle has advanced only about
  0.025 rad mechanical (0.1 rad electrical), allowing starting torque to build
  before the rotating field accelerates. The 2.0 A cutoff remains below the
  motor's documented 4.80 A rated current and above the approximately 1.65 A
  resistive estimate at the fixed 0.9 V motion voltage. The 2.0 V driver clamp,
  24-edge bounds, Hall validity checks, timeouts, and passive PWM-off stopping
  remain unchanged. `pio run -e motor-smoke` succeeds with 25,260/327,680
  bytes RAM (7.7 percent) and 392,356/1,310,720 bytes flash (29.9 percent), and
  the host alignment-classifier test passes.
- 2026-08-06: After confirming no runner or monitor held the serial port, the
  pre-authorized 2.0 A / split-ramp image was flashed to
  `/dev/cu.usbserial-210`. PlatformIO wrote and hash-verified all images and
  completed its normal RTS reset. The assistant did not open serial or send
  `RUN`; physical behavior remains for the operator-run test.
- 2026-08-06: The operator observed a decently smooth single rotation. The
  positive-command stage reached exactly 24/24 legal Hall transitions with no
  reversals, nominal filtered d/q peak 1.378 A, raw phase peak 1.587 A, and
  edge-derived speed magnitude 7.032 rad/s. It stopped with PWM off before
  coast/negative motion only because the firmware expected the custom Hall
  sequence sign to match the arbitrary `POSITIVE` command label; this wiring
  reports that direction as `REVERSE_SEQUENCE`. The acceptance logic now treats
  either sequence as valid for an individual motion stage and requires only
  that the two completed direction stages have opposite Hall sequence signs.
  Current, voltage, Hall-edge, reversal, timeout, coast, and final-PWM bounds
  are unchanged. `pio run -e motor-smoke` succeeds with 25,260/327,680 bytes
  RAM (7.7 percent) and 392,264/1,310,720 bytes flash (29.9 percent), and the
  host alignment-classifier test passes. Evidence is
  `evidence/motor-smoke-20260806T195205Z.txt`.
- 2026-08-06: The pre-authorized Hall-sequence-sign acceptance fix was flashed
  to `/dev/cu.usbserial-210`; all images hash-verified and the uploader
  completed its normal RTS reset. The assistant did not open serial or send
  `RUN`. The operator requested that future uploads omit separate port-
  existence checks; a minimal active runner/monitor conflict check remains.
- 2026-08-06: The sign-fixed run completed the first 24-edge motion and passive
  coast, then collected 24 legal edges during the opposite command but failed
  the old first-edge classifier. Its first transition was reverse and the next
  23 were forward, producing the misleading `direction=REVERSE_SEQUENCE`
  plus `reversals=23`. The operator confirmed the motion was slightly under
  360 degrees and had the same modest roughness as before. The net displacement
  was therefore 23 minus 1, or 22 Hall sectors (approximately 330 mechanical
  degrees), matching the observation. The capture now counts forward and
  reverse transitions independently, derives direction from the dominant
  count, tolerates at most one minority startup-settling edge, and runs until
  24 net transitions rather than 24 total transitions. Host regression cases
  cover the observed 23:1 split and the required 25:1 split. The firmware build
  succeeds with 25,260/327,680 bytes RAM (7.7 percent) and
  392,620/1,310,720 bytes flash (30.0 percent); host tests pass. Evidence is
  `evidence/motor-smoke-20260806T195422Z.txt`.
- 2026-08-06: The pre-authorized net-Hall-direction image was flashed to
  `/dev/cu.usbserial-210`; all images hash-verified and the uploader completed
  its normal RTS reset. The assistant did not open serial or send `RUN`.
- 2026-08-06: The operator-run net-direction image produced terminal PASS.
  Alignment passed with 13 legal Hall edges, no invalid state/transition, and
  0.864 A nominal peak. The first motion completed 24 reverse-sequence net
  edges at an edge-derived -7.032 rad/s; its passive coast recorded one settling
  edge and 1.194 seconds of Hall quiet. The opposite command completed 25
  forward and one reverse transition, yielding the required 24 forward net
  edges at +6.786 rad/s. Its passive coast recorded no edges. Nominal overall
  raw phase peak was 1.571 A, below the 2.0 A filtered d/q gross-fault bound;
  there were no invalid Hall states/transitions or reset. The terminal result
  reported `stopped=YES`, Motor 0 low, Motor 1 zero, and `pwm_state=OFF`.
  Evidence is `evidence/motor-smoke-20260806T200011Z.txt`. This satisfies the
  instrumented pass criteria. The operator subsequently characterized the
  motion as modestly rough and chose to defer that tuning to later
  commissioning, potentially using the bounded SimpleFOC GUI path.
