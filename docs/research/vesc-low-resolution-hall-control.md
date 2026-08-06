# VESC handling of low-resolution Hall sensors

Reviewed: 2026-08-06

## Question

How does VESC obtain usable low-speed FOC control from three digital Hall
sensors, and which parts of that approach could explain the difference between
VESC and this project's current SimpleFOC behavior?

## Answer

VESC does not feed the raw six-step Hall angle directly into the current and
velocity loops at all speeds. Its sensored FOC path is an estimator pipeline:

1. calibrate the electrical angle associated with each observed Hall state;
2. measure the interval and direction between Hall transitions;
3. predict a continuous angle between transitions when speed is high enough;
4. constrain and rate-limit corrections when a new Hall edge arrives;
5. use a PLL-derived speed estimate; and
6. blend from the Hall estimate to the sensorless flux observer as speed rises.

That architecture can make ordinary low-speed running substantially smoother
than using a quantized Hall angle and edge-derived velocity directly. It does
not recover information that the sensors do not provide at standstill or during
very slow travel through one 60-electrical-degree sector. VESC deliberately
disables interpolation below a configurable threshold, and its author states
that sufficiently low-speed smooth control ultimately requires a
higher-resolution encoder.

## Hall angle estimation

Current VESC firmware implements this in
[`foc_correct_hall()`](https://github.com/vedderb/bldc/blob/master/motor/foc_math.c).
The key behaviors are:

- `foc_hall_table[8]` maps each valid digital Hall state to a calibrated
  electrical angle rather than relying only on ideal Hall-state ordering.
- A Hall transition represents approximately `pi / 3` electrical radians. VESC
  divides that angle by the measured signed transition interval to estimate
  electrical speed.
- At a transition, its internal estimate is placed around the midpoint of the
  old and new Hall angles. Between transitions it advances continuously using
  the estimated Hall speed.
- Interpolation proceeds only while the prediction remains within 30 electrical
  degrees of the current Hall center, or while its error indicates that the
  prediction is moving back toward the center. A prediction that is too far
  ahead is pulled back gradually.
- The angle correction is rate-limited before it reaches the current controller;
  the source explicitly says this reduces current spikes when the estimated
  angle changes quickly.
- Below `foc_hall_interp_erpm`, VESC snaps to the closest Hall angle instead of
  interpolating. Its source explains that a reversal between two edges could
  otherwise leave the estimate 60 degrees wrong.

The relevant configuration fields are defined in
[`mc_configuration`](https://github.com/vedderb/bldc/blob/master/datatypes.h),
including `foc_hall_table`, `foc_hall_interp_erpm`, `foc_sl_erpm_start`,
`foc_sl_erpm`, and the FOC PLL gains.

## Speed and sensorless transition

VESC tracks phase and speed with a PI phase-locked loop. Benjamin Vedder
explains that the PLL integrator converges on speed and gives a low-noise speed
estimate, at the cost of tunable phase lag
([discussion](https://github.com/vedderb/bldc/issues/139)). This is different
from treating each Hall edge interval as the velocity signal without a
continuous tracking model.

At higher speed, VESC does not remain dependent on Hall position. It blends the
rate-limited Hall angle into its sensorless observer angle between
`foc_sl_erpm_start` and `foc_sl_erpm`; above the crossover the observer supplies
rotor position. The official
[VESC motor setup wizard](https://vesc-project.com/node/180) describes Hall
sensors as rough position sensors that provide smooth startup from zero RPM and
says that back-EMF-based position estimation takes over at higher RPM.

VESC also contains high-frequency-injection (HFI) modes for suitable motors,
but HFI is not a generic software fix for digital Hall resolution. It depends
on motor saliency, controller support, characterization, and additional tuning.

## Fundamental low-speed limit

Interpolation predicts what happened between the last two edges; it does not
measure what is happening now. During standstill, a reversal before the next
edge, or a long dwell caused by cogging/load, the controller remains blind
inside a 60-electrical-degree sector.

Vedder states this limitation directly:

> Very low speed operation (depending on how low) will always be a problem with
> hall sensors ... there are no updates between the discrete 60 degree position
> updates, and if you spend a lot of time there it becomes a problem to control
> the motor smoothly as you are blind. If you are operating on too low speeds
> there is no other choice than using some sort of encoder with higher
> resolution than hall sensors provide.

Source: [vedderb/bldc issue 139](https://github.com/vedderb/bldc/issues/139#issuecomment-596102897).

## Implications for this firmware

SimpleFOC 2.4.0's stock `HallSensor` reports a sector angle and derives velocity
from Hall transition timing. For this four-pole-pair motor, each Hall sector is
15 mechanical degrees. That quantization is large enough for the estimated
angle, estimated velocity, and current-loop commutation angle to jump even when
the shaft itself is moving smoothly.

At the observed commands there should still be enough edges for interpolation:

| Mechanical speed | Approximate Hall transitions |
|---:|---:|
| 5 rad/s | 19/s |
| 10 rad/s | 38/s |
| 20 rad/s | 76/s |
| 50 rad/s | 191/s |

Therefore a VESC-like Hall estimator could plausibly improve the project's
10–50 rad/s behavior. It would not guarantee smooth standstill or arbitrarily
slow motor motion, especially when cogging prevents the rotor from crossing the
next Hall boundary.

A focused prototype should sit between the Hall GPIO decoder and SimpleFOC and
provide a continuous sensor interface. It would:

- retain the existing illegal-state and illegal-transition checks;
- calibrate or otherwise validate each Hall state's electrical center;
- estimate signed sector period and direction;
- predict electrical angle between edges only above a conservative threshold;
- bound prediction to approximately half a Hall sector;
- rate-limit edge corrections;
- handle reversal and stale-edge timeout explicitly; and
- expose the predicted continuous angle and filtered velocity to SimpleFOC.

This is a control-estimator change, not another velocity-PID adjustment. It
should be evaluated as a separate prototype with timestamped Hall edges,
estimated angle, q-axis current, and shaft behavior. If the required motor speed
falls below the range where that estimator remains observable, the robust
solution is a higher-resolution encoder (or using the gearbox so the motor
itself runs faster), not unlimited integral gain.
