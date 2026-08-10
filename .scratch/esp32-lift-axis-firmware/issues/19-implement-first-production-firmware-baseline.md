# Implement the first production-firmware baseline

Type: task
Status: resolved
Blocked by: 11, 15

## Question

Implement a buildable, host-testable first version of the Ligature production firmware using every decision resolved so far. Include the production operation/fault state machine, homing and position trust, bounded position travel, touchdown and hold behavior, calibration handling, strict serial protocol and observability contract, startup/PWM-off safety, and the seams needed for commissioning-derived configuration and later device verification.

Do not silently resolve the remaining policy decisions. Keep transient-current fault behavior and low-speed geared-motor behavior isolated and explicitly provisional so they can be replaced after those tickets resolve. Do not add the optional composite page-turn operation unless that decision has resolved in its favor. Add automated and protocol-conformance coverage that can be derived from current decisions, build all affected PlatformIO environments, and clearly record which physical behaviors remain unvalidated. This baseline is an iteration point, not production acceptance.

## Answer

Implemented the first baseline as the new `production` PlatformIO environment. The platform-independent `production/controller` owns strict fixed-buffer parsing, the trust-bearing state variants, command permissions, one-command positional-limit override, homing, bounded absolute/relative travel, touchdown/contact settling and current hold, `M24`, stop/fault behavior, heartbeats, and bounded diagnostic capture. The ESP32 adapter establishes low PWM latches before peripheral setup, uses a project-owned Hall sensor with unsigned wrap-safe timing and sticky legal-transition validation, restores compiled electrical candidates without automatic alignment, and translates core drive effects to SimpleFOC. `G73` was not added.

The compiled configuration deliberately remains uncommissioned and therefore boots `COMMISSIONING_ONLY`, keeps PWM off, and rejects `M3`. The provisional current-fault and direct low-speed request policies are isolated in named core methods for replacement after their decision tickets resolve. Blocking SimpleFOC alignment is not used: `M40` has a core adapter seam but fails closed until a cooperative implementation exists. Native Studio traffic is likewise not enabled in this baseline rather than being allowed to bypass production dispatch. The exact endstop pin, assembled-axis conversion/limits/tuning, cooperative alignment, and bounded Studio adapter remain integration work for [Finish and validate the production firmware](20-finish-and-validate-production-firmware.md), not decisions silently made here. [Production firmware baseline](../../../docs/production-baseline.md) records these boundaries and every missing physical validation gate.

Offline conformance passes for strict arm parsing, malformed/state rejection, position trust, override consumption, homing and negative-Z limits, travel completion, `M53`/`M112`, fault recovery, heartbeat framing, touchdown/settled hold/release, fail-closed configuration, and PWM-off capture transfer. Existing motor-tuning and motor-smoke host executables also pass. All five PlatformIO environments build: `hall-validation`, `simplefoc-hall-validation`, `motor-smoke`, `motor-tuning`, and `production`. The production image uses 27,532 bytes RAM and 402,020 bytes flash in the final build. No upload or physical test was performed; boot/reset pulse behavior, stored-calibration startup, Hall/current/endstop behavior, motion, homing, touchdown, hold, stop latency, serial scheduling, and capture timing all remain physically unvalidated.
