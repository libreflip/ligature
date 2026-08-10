# Implement the first production-firmware baseline

Type: task
Status: unclaimed
Blocked by: 11, 15

## Question

Implement a buildable, host-testable first version of the Ligature production firmware using every decision resolved so far. Include the production operation/fault state machine, homing and position trust, bounded position travel, touchdown and hold behavior, calibration handling, strict serial protocol and observability contract, startup/PWM-off safety, and the seams needed for commissioning-derived configuration and later device verification.

Do not silently resolve the remaining policy decisions. Keep transient-current fault behavior and low-speed geared-motor behavior isolated and explicitly provisional so they can be replaced after those tickets resolve. Do not add the optional composite page-turn operation unless that decision has resolved in its favor. Add automated and protocol-conformance coverage that can be derived from current decisions, build all affected PlatformIO environments, and clearly record which physical behaviors remain unvalidated. This baseline is an iteration point, not production acceptance.
