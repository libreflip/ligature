# Define the safe unloaded-motor powered bring-up and test ladder

Type: grilling
Status: resolved
Blocked by: 02, 03

## Question

What exact sequence of bridge-disabled electrical checks and progressively powered Motor 1 tests, with explicit prerequisites, conservative starting limits, expected evidence, pass/fail criteria, abort conditions, and rollback steps, takes this hardware from Hall-only validation through verified phase/enable/current-sense behavior to unloaded closed-loop motor control, with no gearbox, belt, or lift assembly attached?

## Answer

Use a firmware-owned, fixed-step ladder on the standalone unloaded motor. The
ladder uses the existing Mean Well LRS-150-24 at 24 V; an adjustable bench
supply is not a prerequisite, and loss of the surviving bridge is an accepted
test risk. That risk acceptance does not relax the operator-safety controls,
the bounded commands, or the prohibition on uncontrolled motion.

### Invariants and prerequisites

- Motor 0 is prohibited: its driver is never initialized, its PWM pins are
  never configured for motor drive, and GPIO 22 is held inactive. Only Motor 1
  may be tested: PWM GPIOs 26/27/14, enable GPIO 21, current inputs GPIO 35/34,
  and the already-validated Hall inputs GPIO 23/5/13.
- Motor 1 enable must be electrically inactive from reset through driver and
  motor initialization and between every test step. Verify its physical
  polarity with the phases disconnected. Patch or wrap `BLDCMotor::init()`;
  its stock brief zero-PWM enable is not acceptable.
- The NC switch on `I_0`/GPIO 4 is a fail-safe bench-abort input. Closed to GND
  is healthy; pressing it, unplugging it, or breaking its wire opens the input,
  forces Motor 1 enable inactive through a path that remains effective during
  blocking alignment, and latches the abort. It never authorizes motion or
  automatically clears a fault.
- The 1.0 A hard-current trip, ADC-clipping trip, and alignment deadline must
  also remain runnable while the stock alignment call blocks. Blocking
  alignment is prohibited unless these monitors and the NC-switch path can
  independently force Motor 1 enable inactive.
- The Mean Well mains plug remains immediately reachable and is the independent
  physical power-removal method. The software switch is not an emergency stop.
- Clamp the motor body securely, keep the bare shaft and all phase terminals
  clear, and attach no gearbox, belt, suction box, or other load. Power must be
  removed before moving phase wires or multimeter leads.
- Use the AN8009 for continuity, gross-short/isolation, DC voltage, PWM
  frequency, and the one specified current comparison. It cannot qualify PWM
  waveform quality or dead time.
- Powered tests use fixed, named, self-timed serial commands. They accept no
  arbitrary current, voltage, speed, position, or duration parameters. Exactly
  one test may run, nothing queues, and every test returns the bridge to the
  disabled state before reporting or transferring its capture.
- A plain serial terminal is sufficient for this ladder. There is no dedicated
  host harness or host heartbeat; each test's timeout and abort logic lives on
  the ESP32. The later production-protocol conformance harness remains a
  separate deliverable.

### Ordered ladder

1. **Unpowered and phase-disconnected preflight.** With U/V/W disconnected,
   confirm the three phase-to-phase resistance readings are mutually
   consistent, no phase has a gross short to the motor body, supply polarity is
   correct, Motor 1 is routed U/V/W to A1/B1/C1, and the NC switch has continuity
   when healthy and opens when pressed. Reject damaged insulation, loose
   terminals, unexpected continuity, or inconsistent winding readings.
2. **Powered logic preflight with phases still disconnected.** On repeated
   power-up/reset/serial-reconnect trials, verify GPIO 21 stays at the measured
   inactive level, GPIO 22 remains inactive, no phase command runs, and both
   current channels establish stable zero-current baselines without clipping.
   Exercise the NC switch and a disconnected switch wire and require a latched
   abort. With the bridge disabled, confirm the configured PWM frequency on
   GPIOs 26/27/14 using the AN8009. This proves routing and frequency only, not
   waveform shape or dead time.
3. **Connect phases with power removed.** Unplug mains, verify the supply is
   de-energized, connect motor U/V/W to A1/B1/C1, secure the meter leads and
   motor, restore the NC switch to healthy, and place the mains plug within the
   operator's reach. Re-run the boot-disabled checks before the first enable.
4. **Bounded static-vector pulses.** Run separately confirmed A, B, and C vector
   commands. Each begins at approximately 0.25 V differential-equivalent
   output (about 1% of the 24 V bus), lasts at most 100 ms, captures current and
   Hall data, and disables unconditionally. A separately authorized retry may
   use 0.50 V; there is no automatic increase. Pass requires only a bounded
   twitch or no motion, plausible current-channel response, no continuous
   rotation, no clipping, no illegal Hall transition, and peak current below
   0.50 A.
5. **Minimal absolute-current check.** After the pulses pass, use the AN8009 in
   series with one energized phase for one firmware-timed, two-second static
   hold near 0.25 A. The command cuts off automatically at two seconds and
   aborts above 0.50 A. The firmware and meter must agree within the larger of
   25% or 0.05 A; both onboard channels must respond plausibly without clipping.
   This is a single-point plausibility check, not a linearity calibration.
6. **Supervised alignment.** Run stock blocking SimpleFOC sensor/electrical-zero
   and current-sense alignment only as an explicit test command, after proving
   that the independent NC-switch, hard-current, clipping, and deadline paths
   disable the bridge during a blocked operation. Start around 0.50 A. If
   motion is insufficient, disable, inspect the capture, and allow a separately
   confirmed increase no larger than 0.25 A. Never exceed 1.0 A and never
   increase or retry automatically.
   SimpleFOC's learned current pin mapping and gain signs must be reconciled
   with the static-vector evidence rather than accepted silently. Success
   requires a finite electrical zero, known sensor direction, successful
   current-sense alignment, legal Hall transitions, no fault, and immediate
   disable at completion. Report all values but retain them in RAM only; reset,
   abort, or failure discards them and ordinary startup never aligns.
7. **Current-loop gate.** Use `foc_current` with a conservative model-derived
   50 Hz bandwidth from the documented 1.03 ohm and 0.43 mH phase values:
   approximately `P=0.135`, `I=324`, `D=0` for both q and d loops. Begin with a
   1.0 V internal voltage-output clamp. First enable at a 0 A target for one
   second and require zero Hall transitions, then disable. Run separately
   authorized `+0.25 A` and `-0.25 A` q-current commands for 100 ms each, with a
   disabled cooldown between them. Settled q-current must be within 25% of the
   requested magnitude with the correct sign, peak measured current must remain
   below 0.50 A, and there must be no clipping, illegal Hall transition, or
   dropped sample. Record d-current for later tuning but do not gate on it yet.
8. **Velocity gate.** Use a 0.50 A operational current limit and the independent
   1.0 A hard trip. In separate commands, ramp from zero to `+5 rad/s` over one
   second, hold three seconds, ramp to zero, confirm stopped, and disable; then
   repeat at `-5 rad/s`. After settling, mean velocity must be within 20% of
   target, no sample may exceed 7.5 rad/s, Hall direction signs must be opposite
   and internally consistent, and current saturation may not persist for more
   than 250 ms. A stop requires no further Hall edges and a reported zero
   velocity before terminal success.
9. **Position gate.** Capture the start angle. In separate confirmed commands,
   move to `start + 2*pi rad` and then `-2*pi rad` back to the captured start,
   using the 5 rad/s velocity ceiling and 0.50 A operational current limit.
   Each move has a four-second timeout, approximately 24 net legal Hall steps,
   no sustained oscillation, and a terminal tolerance of one Hall step
   (`2*pi/24`, about 0.262 rad). The return must finish within one Hall step of
   the original position. Obstruction, timeout, or sustained saturation is a
   failure, never a successful partial position move.
10. **Short repeatability acceptance.** Only after every preceding gate passes
    once in the same powered session may one fixed automatic sequence make ten
    round trips between the captured start and `start + 2*pi rad`. Use the same
    limits and dwell stationary for 250 ms at every endpoint. All twenty moves
    must finish within one Hall step, with no illegal transition, ADC clipping,
    dropped capture, control-loop overrun, or saturation longer than 250 ms;
    final position must return within one Hall step of the start. This is a
    short repeatability check, not a thermal or endurance soak.

The 1.0 A measured phase-current trip is the hard ceiling for the entire
unloaded ladder. Current is the operator-facing torque limit. Voltage values
are internal PWM/current-loop protection only and may be raised solely enough
to obtain the separately approved current; no automatic voltage or current
escalation exists.

### Evidence and result handling

Every powered step records a fixed-size, preallocated ESP-side capture with a
64-bit timestamp, sequence number, raw Hall state/sector, shaft angle and
velocity, targets, current setpoint, q/d current, q/d voltage, electrical angle,
loop time, controller/test state, fault bits, and dropped/overrun counts. It
performs no serial formatting or transmission in the active control path. After
the bridge is disabled, the operator saves the terminal result and requested
capture through the plain serial terminal, together with firmware/configuration
identity, exact step name, AN8009 reading where required, and observations of
motion, noise, vibration, smell, heat, or visible instability. A firmware pass
without its capture or operator observation is incomplete, not passed.

### Abort and rollback

The NC switch, current above 1.0 A, ADC clipping, illegal Hall state or
transition, enable-invariant violation, nonzero current while commanded
disabled, overspeed, operation timeout, reset/brownout, or internal deadline
failure immediately commands zero PWM, makes GPIO 21 inactive, disarms, freezes
the capture, and latches the result. The operator uses the NC switch or pulls
mains for unexpected sound, vibration, heat, odor, smoke, visible instability,
or any case where firmware does not disable promptly.

- A safety fault (overcurrent, clipping, Hall/enable invariant violation,
  reset/brownout, current while disabled, smoke/odor, or emergency mains
  removal) requires power removal, discarded alignment, wiring/hardware
  inspection, and restart at the phase-disconnected preflight.
- A deliberate NC-switch abort requires inspection and explicit fault clearing;
  phases may remain connected only when the abort was intentional and no anomaly
  occurred.
- A bounded test failure (no motion, tracking error, timeout, or saturation
  below the hard trip) freezes evidence and permits only an explicitly approved
  parameter adjustment and retry from that gate or an earlier one.

Nothing retries, continues, re-arms, or resumes after serial reconnect or reset.

This ladder can establish provisional unloaded Motor 1 operation only. It does
not validate PWM waveform/dead time, current-sense linearity or production
ampere accuracy, persistent calibration validity, thermal limits, long-duration
reliability, gearbox/lift behavior, homing, touchdown, representative-book
loads, or the production serial contract.
