# ESP32 lift-axis motor firmware journey

Label: wayfinder:map

## Destination

Produce a decision-complete, safety-gated ESP32 firmware and verification route from the validated Hall inputs through supervised powered bring-up, commissioning and tuning, and production acceptance on the assembled lift axis with representative books. Every production behavior must have explicit test evidence.

## Notes

- Scope is the ESP32 firmware and a host-side protocol conformance harness only. The Raspberry Pi production client, touchscreen UI, and whole-bookscanner orchestration are outside this effort.
- Treat `docs/hall-validation.md` as current hardware evidence. Treat `docs/plan/bldc-driver.md` and the T20/T32 material in `docs/plan/tasks.md` as intended direction, not verified facts or a frozen command contract.
- Use the vocabulary in `CONTEXT.md`. Use `grilling` and `domain-modeling` for decision tickets, `research` for current upstream facts, and `diagnosing-bugs` when physical tests produce anomalous behavior.
- Planning is the default. Add manual `wayfinder:task` tickets only when a measurement or physical action is required before a later decision can be made.
- Separate compilation and automated-test evidence from physical hardware validation. Never claim motor, Hall, current-sense, endstop, homing, or touchdown behavior without the corresponding device test.
- Boot, reset, serial reconnect, and ordinary arming must not initiate motion. Any motion-capable SimpleFOC alignment requires an explicit supervised commissioning operation.
- A commissioning GUI may tune bounded parameters and inspect diagnostics, but is not a production control interface and must obey the same safety gates.
- Normal travel is current-limited position control: it succeeds only by reaching its target and otherwise faults. Touchdown is the sole intentional contact-seeking operation. Generalized modal torque-based travel is excluded.
- Exactly one motion or calibration operation may be active. Status and immediate stop remain available; other commands fail busy and nothing queues. Immediate stop cuts PWM, cancels the operation, disarms, and initially invalidates homing.
- Production serial output is limited to immediate operation acknowledgment, one terminal success or failure, requested status, and unsolicited hard faults. High-rate diagnostics use triggered, timestamped ESP-side buffering and may transfer only when transfer cannot disturb active control.
- The G-code-style protocol is a semantic direction, not a frozen command table. Command spellings and framing remain revisable.
- Missing or invalid calibration should fail closed if a sufficiently simple validity scheme can achieve it; commissioning must remain possible without silently granting production motion.

## Decisions so far

- [Establish current SimpleFOC commissioning-tool options](issues/01-establish-simplefoc-commissioning-tool-options.md) — Evaluate WebController through a firmware-owned bounded Commander adapter; keep it development-only and retain ESP-side capture for timing evidence.
- [Establish SimpleFOC startup, Hall, and current-sense constraints](issues/02-establish-simplefoc-startup-hall-and-current-sense-constraints.md) — Stored electrical/current-sense calibration can avoid alignment, but startup enable, blocking commissioning, Hall timing, and board current metrology require explicit fixes and tests.
- [Inventory the lift-axis hardware and test safeguards](issues/03-inventory-lift-axis-hardware-and-test-safeguards.md) — Use only Motor 1 for standalone unloaded testing; Hall inputs are validated, while bridge/current behavior remains gated by preflight and the reachable mains plug is the physical power cut-off.

## Not yet specified

- The actual numeric current, voltage-protection, velocity, acceleration, position-tolerance, timeout, controller-gain, and thermal limits. These depend on powered characterization and the selected control architecture.
- The exact manual characterization work needed to tune the unloaded motor, assembled lift axis, and representative-book behaviors. This will graduate into task tickets after the powered bring-up ladder is decided.
- Whether a commanded immediate stop can ever preserve trusted position. This may graduate only after the stop-position experiment and its acceptable error bound are defined.
- The contact-detection thresholds, debounce/dwell rules, stationary-hold tolerances, and force-fit shape. These depend on lift-axis and representative-book measurements.
- Long-duration soak duration and environmental/load corners. These depend on observed failure modes and the final production state machine.

## Out of scope

- Raspberry Pi production-client implementation, UI behavior, page-turn sequencing, cameras, pneumatics, and relay control. Only their ESP32 wire-contract expectations matter here.
- A general-purpose torque/compliant travel mode. Dedicated touchdown and force-calibration behaviors cover the known contact-controlled cases.
- A cross-controller vacuum-state interlock or notification protocol; it requires bookscanner orchestration beyond this ESP32-only destination.
- Hardware redesign, including adding a hardware emergency stop. Supervised powered tests must nevertheless provide an immediate physical means to remove motor power.
