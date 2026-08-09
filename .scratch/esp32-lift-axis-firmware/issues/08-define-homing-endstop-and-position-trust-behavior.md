# Define homing, endstop, and position-trust behavior

Type: grilling
Status: resolved
Blocked by: 07, 13

## Question

How should Ligature implement and validate its fixed top-endstop seek/back-off/slow-locate homing sequence, positive-downward Z frame, persisted top/bottom soft limits, and immediate `ENDSTOP_UNEXPECTED` fault outside homing? Define switch filtering, seek/pull-off/locate bounds, command rejection at each soft limit, the allowed homing exception, and position-trust invalidation. Emergency stop (`M112`), reset, and hard faults must clear homing; routine abort (`M53`) is intended to preserve it, so what repeated assembled-axis stop-position experiment and error bound are required before that preservation is accepted?

## Answer

Use the newer lift-axis coordinate convention from `CONTEXT.md`, superseding the stale positive-downward wording in this ticket and `docs/plan/ligature.md`: the upper endstop reference is `Z=0`, upward increases Z, downward decreases Z, and ordinary lift-axis positions are negative.

Treat the configured-polarity endstop input as an immediate signal with no debounce or noise filtering for now. Any raw assertion commands zero PWM in the same control iteration. Noise mitigation may be added only if device evidence shows it is needed.

Homing requires an armed controller and is the only operation allowed to assert the endstop intentionally or cross the top soft limit. It does not use a second slow locate:

1. If the switch starts inactive, seek upward at the configured homing speed until its first assertion, then command zero PWM immediately.
2. Reverse downward at the configured pull-off speed until the switch releases. If the switch was already active when homing began, start with this release move instead of seeking into it again.
3. Define the release edge as `Z=0`, then continue downward exactly 2 mm. Successful homing ends clear of the switch at `Z=-2 mm`, which is also the top soft limit.

Bound the upward seek by the commissioned full physical travel plus a modest allowance. Bound switch release to 5 mm. Give both phases deadlines derived from their distance and configured speed plus the normal startup allowance, subject to the immutable maximum operation duration. Exceeding a distance or time bound, failing to release, losing valid Hall feedback, or interrupting homing commands zero PWM, reports `HOMING_FAILED`, disarms, and leaves position untrusted. It does not enter the persistent software `Fault` state; after correcting the cause, the operator may arm and home again.

The compiled commissioned configuration contains the bottom soft limit and homing parameters. The top soft limit is fixed by the homing result at `Z=-2 mm`. Ordinary commands must have every target and multi-phase path inside the inclusive interval from the bottom soft limit through the top soft limit; reject the entire command rather than clamp it. Monitor actual position as well: crossing either limit by more than the commissioned position tolerance stops the operation, reports a soft-limit travel failure, and disarms. Homing has the sole top-limit exception. The previously accepted one-command positional-limit override remains the explicit exception for its next relative travel move. Whether touchdown may cross the bottom limit remains for the touchdown decision.

Outside active homing, any endstop assertion immediately commands zero PWM, reports unsolicited `ENDSTOP_UNEXPECTED`, enters the software `Fault` state, and invalidates position trust. Clearing a software fault never requires a physical reset button: the host explicitly clears it, re-arms, and homes where trust was invalidated.

Position remains trusted while Hall position has been observed continuously since successful homing. Preserve it across `M5`, `M53`, and `M112` when the ESP32 remains powered and Hall tracking remains valid; these commands do not inherently require rehoming. Invalidate it on reset, watchdog or brownout reboot, invalid or discontinuous Hall feedback, failed or interrupted homing, and unexpected endstop contact. A current-sense or other hard fault may preserve position when Hall tracking remained valid, although software fault-clear and re-arm are still required. An invalid Hall sequence reports a fault and requires clear, arm, and home before ordinary movement. There is no brownout recovery that preserves the frame.

Trust the motor Hall observation as the position source and deliberately ignore possible gearbox or belt slip for now. No repeated `M53` stop-position experiment or additional preservation error bound is required.
