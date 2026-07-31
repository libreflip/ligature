# Execute the safe unloaded-motor powered bring-up

Type: task
Blocked by: 04

## Question

Implement only the fixed-step diagnostic firmware needed by [Define the safe unloaded-motor powered bring-up and test ladder](04-define-safe-powered-bring-up-and-motor-test-ladder.md), build it, flash it to the Hall-validated board, and execute every supervised gate on the standalone Motor 1 hardware. What do the retained ESP-side captures, AN8009 check, terminal results, and operator observations establish about enable/PWM routing, current-sense plausibility, alignment, current control, bidirectional velocity and position control, faults, timing, and ten-round-trip repeatability under the resolved provisional limits?

Do not proceed past a failed gate, do not treat compilation as device evidence, and use `diagnosing-bugs` if physical behavior is anomalous. Record actual measurements separately from configured targets. This task supplies the powered evidence required before production control limits or the assembled lift-axis ladder can be decided.
