# Choose the control architecture and travel-fault criteria

Type: grilling
Status: resolved
Blocked by: 02, 04, 05, 14

## Question

Which SimpleFOC current, velocity, and position-loop structure should implement Ligature's normal position travel, given the coarse Hall velocity evidence from the unloaded smoke test; how should model-based limiting protect first alignment before measured `foc_current` control is trusted; and what current, voltage, velocity, acceleration/output-ramp, thermal, target-tolerance, timeout, saturation, tracking-error, and obstruction criteria make a travel move succeed versus fail and disarm? Keep generalized modal torque travel out of scope: touchdown is the only production contact-seeking move.

## Answer

Normal travel uses SimpleFOC's standard `angle` → velocity → measured
`foc_current` cascade. The 10:1 gearbox makes the Hall position resolution
approximately 0.2 mm at the lift axis, which is acceptable for this application;
open-loop motion remains commissioning-only. Production travel is unavailable
until measured-current control has passed commissioning. Firmware must never
silently fall back to voltage-mode travel.

The first electrical alignment is the accepted exception. It is an explicit,
supervised commissioning operation using an immutable alignment-voltage ceiling
derived from a conservative winding-resistance lower bound and approved
alignment-current ceiling. A gross current observation may abort alignment when
usable, but is not represented as calibrated protection. Ordinary startup uses
a valid stored electrical calibration and does not align.

A travel move succeeds only after entering a configurable target tolerance of
at least one output-side Hall sector and then completing a Hall-quiet settle
window. It does not depend on a precise zero-velocity estimate. Its simple
deadline is distance divided by requested speed plus a fixed startup/settling
allowance, capped by the immutable maximum motion duration; commands whose
calculated deadline exceeds that cap are rejected. Touchdown and commissioning
operations have separate deadlines and completion rules.

Use `P_angle.output_ramp` to slew the velocity request and
`PID_velocity.output_ramp` to limit current-request slew. Do not add a custom
trajectory planner. Assembled-axis commissioning with representative loads
sets the tuning profile's operating current limit, internal voltage clamp,
velocity limit, ramp rates, position tolerance, settle window, and watchdog
times below immutable firmware ceilings.

Keep electrical protection minimal: normal motion uses the calibrated
operating `current_limit`; exceeding the immutable current ceiling, clipped ADC
feedback, or invalid/non-finite current feedback stops and disarms immediately.
`voltage_limit` is an internal clamp rather than a separate fault threshold.
Reaching an operating current or voltage limit is not by itself a fault. After
a startup grace period, sustained limiting combined with no net Hall progress
for a configurable dwell is an obstruction/travel failure. The overall deadline
catches other inability to reach the target. There is no separate trajectory
tracking-error calculation.

Every accepted travel that does not satisfy the success criteria emits an
explicit terminal failure, commands zero PWM, and disarms; it never reports a
partial move as success or fails silently. Exact fault latching and recovery
states remain for the production state-machine decision.

Do not add runtime thermal protection or a separate overspeed detector now.
Retain SimpleFOC's configured velocity limit and the general maximum-motion
bound; add further protection later only if device evidence requires it.
Generalized torque/current travel remains out of scope. `foc_current` is the
inner torque loop for position travel, while touchdown is the sole production
contact-seeking operation.
