# Define the assembled lift-axis powered bring-up and load-test ladder

Type: grilling
Status: resolved
Blocked by: 04, 06, 07, 14

## Question

After the unloaded Motor 1 smoke result and the control/calibration architecture are established, what Ligature Stage 2 installation inspections, mechanical restraints, endstop and positive-downward Z checks, conservative measured-current/voltage/velocity/soft limits, progressively loaded stages, expected evidence, pass/fail criteria, abort conditions, and rollback steps take the gearbox/belt/suction-box lift axis from first assembly through representative-load characterization before production homing, travel, touchdown, and page-turn acceptance?

## Answer

Stage 2 uses the progressively completed production firmware on the assembled suction-box lift axis. It is not a separate load-characterization campaign. Its acceptance boundary is deliberately narrow: establish direction, home, calibrate linear distance, and determine the bottom soft limit. Representative-book and progressively loaded travel tests are not required by this stage. The same firmware may later acquire touchdown behavior after [Define touchdown, stationary hold, and press-level behavior](10-define-touchdown-hold-and-force-behavior.md) resolves that behavior; a separate decision ticket does not imply another firmware image or physical setup.

### Permanent one-command positional-limit override

Production firmware provides a reusable command whose protocol spelling remains for the production-protocol decision. It authorizes exactly the next accepted relative travel command to bypass only the homing requirement and positional soft-limit checks. The authorization is consumed as soon as that travel is accepted; a rejected command does not consume it, and every later overridden move needs a fresh authorization.

The override never bypasses configured current, voltage, velocity, controller, or other immutable safety ceilings. It permits no absolute target while position is untrusted, does not weaken the physical-endstop rule, and does not turn endstop contact into success: only homing may intentionally touch the upper endstop. There is no special override-only displacement or duration cap. The ordinary deadline still bounds active travel while it is attempting to reach its target. After successful settling completes the travel operation, the controller may hold the reached position indefinitely under its normal electrical ceilings. Stop, fault, disarm, or reset clears any pending authorization; stop and fault behavior remain available while the move runs.

### Stage 2 sequence

1. Assemble and inspect the gearbox, belt, suction box, endstop, and travel path. Position the suction box away from both mechanical ends, keep the supervised physical power-removal action reachable, and use the accepted geared-motor commissioned configuration and ceilings without a separate limit-escalation test.
2. Issue the positional-limit override and command a deliberately small positive relative move. Visually confirm that positive motion is physically upward. Up is increasing Z. If the box moves down, behaves unexpectedly, or cannot stop, cut PWM, correct the configured motor/sensor/coordinate direction, and repeat from a safe position; never compensate mentally or continue with an inverted frame.
3. Run the production homing operation against the upper endstop. Successful homing establishes the upper endstop as `Z=0`; positions below it are negative. The exact seek, release, locate, pull-off, and upper-soft-limit behavior belongs to [Define homing, endstop, and position-trust behavior](08-define-homing-endstop-and-position-trust-behavior.md).
4. From the homed frame, command several known motor rotations, manually measure suction-box displacement, calculate millimetres per motor revolution, and repeat once as a sanity check. The operator supplies the accepted value for the source-controlled commissioned configuration; firmware does not persist it automatically.
5. With the accepted distance conversion active, manually jog downward and measure the deepest mechanically safe operating position. Choose and supply a safely shallower negative Z value as the bottom soft limit. The supplied limit already incorporates the desired physical margin; there is no separate bottom-margin configuration.

Any wrong direction, unexpected endstop behavior, controller fault, mechanical interference, belt slip, or inability to stop aborts the current step with zero PWM. Correct the cause before retrying; Stage 2 does not raise electrical limits merely to force acceptance. Passing all five steps establishes the coordinate frame and calibrated travel envelope needed by later homing, travel, touchdown, state-machine, and protocol acceptance decisions. It does not by itself validate touchdown, representative books, page turning, soak behavior, or production acceptance.
