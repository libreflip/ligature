# SimpleFOC Hall control and motor-parameter guidance

Research date: 2026-08-04

## Scope and source boundary

This note answers the issue-14 questions raised by the unloaded Motor 1 tests:
whether a higher closed-loop speed is a better Hall-sensor test, whether motor
resistance and inductance should be measured, and which upstream examples are
actually applicable.

The behavioral baseline is the repository-pinned SimpleFOC 2.4.0 source, not
only the live documentation. Primary sources are the
[v2.4.0 tag](https://github.com/simplefoc/Arduino-FOC/tree/v2.4.0), official
SimpleFOC documentation and examples, the checked-in
[42BSA78-24-01 datasheet](../pdf/42BSA78-24-01.pdf), and the checked-in
[MKS ESP32 FOC V1.0 schematic](../pdf/MKS%20ESP32%20FOC%20V1.0%20schematic.pdf).

## Conclusions

1. A higher **motor-side** Hall velocity is more representative once the
   current loop is credible. At 2 rad/s this four-pole-pair motor produces only
   about 7.6 Hall edges/s; at 10 rad/s it produces about 38.2 edges/s. The 10:1
   gearbox makes 10 motor-rad/s only 1 output-rad/s and multiplies effective
   output-side Hall position resolution by ten.
2. Higher speed does not improve Hall angular resolution. The sensor still
   supplies one 60-degree electrical sector, or 15 mechanical degrees on this
   motor, per transition. It only makes velocity updates more frequent and
   allows inertia to average some ripple. SimpleFOC itself warns that Hall
   quantization makes low-speed operation unsmooth in its
   [position-sensor overview](https://docs.simplefoc.com/position_sensors#hall-sensors).
3. Do not run the next closed-loop test yet. The latest open-loop ramp tracked
   its commanded trajectory but reported 1.000 A q current against a 0.750 A
   request at 8.9 rad/s. A higher closed-loop test would combine an unresolved
   current loop with the coarse Hall velocity loop.
4. Measuring this motor's resistance and inductance is worthwhile. SimpleFOC
   2.4.0 has a model-based `tuneCurrentController(bandwidth)` helper that uses
   those values; the smoke firmware's current gains were arbitrary example
   values rather than motor-derived values.

## What the official Hall examples establish

The official v2.4.0
[Hall velocity example](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/examples/motion_control/velocity_motion_control/hall_sensor/velocity_control/velocity_control.ino)
uses `MotionControlType::velocity`, `PID_velocity.P=0.2`,
`PID_velocity.I=2`, a 10 ms velocity filter, and the default voltage-torque
mode. It demonstrates API wiring, not portable gains or current-loop tuning.
The official
[Hall angle example](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/examples/motion_control/position_motion_control/hall_sensor/angle_control/angle_control.ino)
uses the same generic velocity-loop values and a 4 rad/s velocity limit.

The newer hardware-specific
[Hall plus current-control example](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/examples/hardware_specific_examples/Silabs/efr32_torque_velocity_6pwm/efr32_torque_velocity_6pwm.ino)
uses `P=1`, `I=100` for q/d current loops, but it targets a different motor,
driver, low-side current sensor, MCU, and loop timing. Matching those numbers in
this project does not validate them.

Pinned 2.4.0 `HallSensor` reports one sector angle between interrupts and a
one-edge-period velocity. Its
[`getVelocity()` implementation](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/sensors/HallSensor.cpp#L125-L140)
returns the most recent edge-derived velocity until it becomes stale, then
returns zero. This explains why a velocity loop is especially quantized when
edges are 131 ms apart at 2 rad/s. The live Hall documentation describes a
`velocity_max` outlier setting, but the pinned 2.4.0 implementation does not
read that field, so it cannot solve this behavior.

## Why the motor parameters matter here

The manufacturer specifies:

- four pole pairs (eight poles);
- 1.03 ohm phase resistance at 25 degrees C, plus/minus 10 percent;
- 0.43 mH phase inductance at 1 kHz, plus/minus 20 percent;
- 4.41 V/krpm back EMF, plus/minus 5 percent.

The local smoke firmware currently uses `P=1`, `I=100`, and a 2 ms current
filter for both q and d. SimpleFOC's official
[current-loop tuning guide](https://docs.simplefoc.com/tuning_current_loop)
and pinned
[`tuneCurrentController()` source](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L542-L576)
use:

```text
P = L * 2*pi*f_bw
I = R * 2*pi*f_bw
Tf = 1 / (2*pi*f_bw*5)
```

Using the datasheet nominal values, the present `P=1` corresponds to about
370 Hz bandwidth while `I=100` corresponds to about 15.5 Hz. They therefore do
not describe one consistent R/L-based current loop. For illustration only, a
100 Hz model-based starting point would be approximately `P=0.270`, `I=647`,
and `Tf=0.318 ms`. The actual bandwidth must be chosen after measuring the
`loopFOC()` rate; the official guide suggests beginning around 5--10 percent of
that rate. These formula results are starting gains, not device validation.

Resistance and inductance are not required merely to instantiate a
`BLDCMotor`, but they are required for this automatic PI tuning and are used for
inductive/cross-coupling compensation in current-control modes. KV is useful
for estimated-current/back-EMF compensation but is not the immediate blocker
for the present measured `foc_current` test. See the official
[motor-parameter overview](https://docs.simplefoc.com/bldcmotor#step-54-motor-parameters---phase-resistance-inductance-and-kv-rating).

SimpleFOC also offers energized `characteriseMotor()`, but it depends on the
board's current measurement and moves the project back into an active powered
characterization procedure. An independent LCR/DCR check is the cleaner next
step while current scale and loop behavior remain provisional.

## Recommended LCR/DCR measurements

Disconnect the three motor phase leads from the MKS board and remove 24 V
before connecting the meter. Keep the motor at room temperature and stationary.

1. Record the LCR meter make/model and available test level.
2. Perform open/short compensation at the ends of the actual probes or Kelvin
   clips.
3. Measure DC resistance for U-V, V-W, and W-U. Prefer the meter's DCR or
   four-wire mode; series resistance reported at 1 kHz is not a substitute for
   copper DCR. Record raw line-to-line values and temperature.
4. Select series-equivalent `Ls/Rs`, 1 kHz, no DC bias, and the lowest practical
   AC test level. Measure U-V, V-W, and W-U and record L, Rs, and Q/dissipation
   if available.
5. Repeat the inductance readings at several rotor positions, or at least note
   the observed minimum and maximum while repositioning the shaft. Rotor-angle
   variation is useful evidence of saliency and measurement repeatability.

Send the raw pairwise readings without converting them. For a three-wire wye
motor, official SimpleFOC resistance guidance uses
`R_phase = R_line-to-line / 2`; for delta it uses
`R_phase = 1.5 * R_line-to-line`. The motor documentation does not explicitly
state its winding connection, so the datasheet value and the three raw readings
should be reconciled before loading a converted value into firmware. The
conversion guidance is in SimpleFOC's
[phase-resistance measurement guide](https://docs.simplefoc.com/phase_resistance).

## Recommended issue-14 sequence

1. Measure pairwise DCR and 1 kHz inductance as above. Do not run the motor for
   this step.
2. Add the reconciled phase resistance/inductance to the diagnostic, report the
   measured `loopFOC()` period, and replace the arbitrary current PI/filter
   values with one explicit conservative model-based bandwidth. Keep the 0.75 A
   limit, 1.0 A reported-current cutoff, and 2.0 V internal voltage clamp.
3. Repeat the existing bounded 0-to-10 rad/s **open-loop** trajectory. This is
   the red-capable current-loop check: q current should track 0.75 A without
   reaching the 1.0 A cutoff, while d current remains controlled and the
   operator reports whether the electrical roughness improves.
4. Only after that check passes, run a bounded closed-loop Hall test around
   10 rad/s motor-side, with a target ramp and edge/time limits. Evaluate actual
   edge-derived speed, q/d current, illegal transitions, direction, and coast or
   stop separately.
5. If the higher-speed closed loop is controlled, repeat it in the opposite
   direction. If it still jumps despite credible current regulation, stop
   retuning issue 14: the remaining decision is Hall interpolation/control
   architecture or an encoder, not more current.

This sequence tests the user's gearbox hypothesis without pretending that
higher speed fixes Hall angular quantization or bypasses the measured current-
loop fault.

## Actual disconnected-motor measurements

Measurements reported on 2026-08-05, all at one rotor position:

| Pair | DCR | Ls at 1 kHz | Rs at 1 kHz |
| --- | ---: | ---: | ---: |
| U-V | 1.09 ohm | 429.4 uH | 1.139 ohm |
| V-W | 1.09 ohm | 433.0 uH | 1.143 ohm |
| W-U | 1.09 ohm | 514.2 uH | 1.163 ohm |

The three DCR readings agree within the reported plus/minus 0.01-ohm
uncertainty. The 1-kHz series-resistance readings agree within approximately
2.1 percent. This is evidence of balanced winding resistance, not an open or
obviously damaged phase. W-U inductance is approximately 19 percent higher
than U-V at this rotor position. All raw readings nevertheless fall within the
manufacturer's stated 1.03-ohm plus/minus 10-percent and 0.43-mH plus/minus
20-percent ranges if those datasheet numbers are interpreted as terminal-pair
measurements.

Do not convert these values yet. A balanced three-terminal measurement cannot
by itself identify internal wye versus delta construction, and the datasheet's
`resistance/phase` and `inductance/phase` labels appear numerically consistent
with the measured line-to-line values. First repeat Ls for each pair over rotor
position and record minimum/maximum values. If the high value follows rotor
angle rather than remaining attached to W-U, it is rotor saliency/geometry; if
W-U remains high at every rotor position, investigate phase asymmetry or the
measurement setup before deriving controller gains.

The operator then sampled each pair at six approximately even positions over
90 mechanical degrees:

| Pair | Samples (uH) | Minimum | Maximum |
| --- | --- | ---: | ---: |
| U-V | 430, 439, 525, 426, 433, 431 | 426 uH | 525 uH |
| V-W | 437, 441, 529, 434, 530, 440 | 434 uH | 530 uH |
| W-U | 424, 518, 420, 425, 515, 424 | 420 uH | 518 uH |

The elevated reading moves among all three terminal pairs with rotor position,
and their overall ranges closely agree. This falsifies a W-U-specific
inductance defect and identifies normal rotor-angle saliency. For a terminal
equivalent wye model, the measurements give approximately 0.545 ohm per phase,
0.210 mH minimum-axis inductance, and 0.265 mH maximum-axis inductance. Treat
these as initial controller-model values rather than a declaration of the
motor's inaccessible physical winding connection. The ratio and electrical
time constants are unaffected by applying the same line-to-phase factor to R
and L.
