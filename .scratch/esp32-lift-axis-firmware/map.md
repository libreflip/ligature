# Ligature lift-axis firmware journey

Label: wayfinder:map

## Destination

Produce a decision-complete implementation and verification route for `docs/plan/ligature.md`, from the validated Hall inputs through supervised powered bring-up, commissioning and tuning, and production acceptance on the assembled lift axis with representative books. Every production behavior must have explicit test evidence.

## Notes

- Scope is the ESP32 firmware and a host-side protocol conformance harness only. The Raspberry Pi production client, touchscreen UI, and whole-bookscanner orchestration are outside this effort.
- Treat `docs/hall-validation.md` and resolved device-test tickets as current hardware evidence. Treat §§1–16 of `docs/plan/ligature.md` as the target-state behavioral input, except where the document explicitly marks a proposal as unconfirmed; treat §17 and the T20/T32 material in `docs/plan/tasks.md` as status/direction to re-verify rather than device evidence. When copied status or hardware claims conflict with this repo's newer recorded evidence, the local evidence wins.
- Use the vocabulary in `CONTEXT.md`. Use `grilling` and `domain-modeling` for decision tickets, `research` for current upstream facts, and `diagnosing-bugs` when physical tests produce anomalous behavior.
- Planning is the default. Add manual `wayfinder:task` tickets only when a measurement or physical action is required before a later decision can be made.
- For standalone unloaded Motor 1 bring-up, accept the replaceable bridge and motor as development consumables. Use the completed two-command, fixed-bound smoke test rather than adding hardware-abort, meter-qualification, staged-gate, precision-characterization, or high-rate-capture prerequisites; basic operator precautions still apply.
- The MKS ESP32 FOC V1.0 schematic has no separate motor-driver enable input; GPIO 21/22 are unconnected. Software inactivity means zero/low PWM commands, not bridge-power isolation. Only the supervised mains action removes bridge power.
- Separate compilation and automated-test evidence from physical hardware validation. Never claim motor, Hall, current-sense, endstop, homing, or touchdown behavior without the corresponding device test.
- Boot, reset, serial reconnect, and ordinary arming must not initiate motion. Any motion-capable SimpleFOC alignment requires an explicit supervised commissioning operation.
- A commissioning GUI may tune bounded parameters and inspect diagnostics, but is not a production control interface and must obey the same safety gates.
- Normal travel is current-limited position control: it succeeds only by reaching its target and otherwise faults. Touchdown is the sole intentional contact-seeking operation. The proposed generalized `M50`/`M51` modal torque travel in `docs/plan/ligature.md` §11 remains excluded from this map.
- Exactly one motion or calibration operation may be active. Status and immediate stop remain available; other commands fail busy and nothing queues. Emergency stop (`M112`) cuts PWM, cancels the operation, disarms, latches `Fault`, and invalidates homing; routine attempt abort (`M53`) cuts the active move but remains armed and homed. Neither may resume motion implicitly.
- Production serial output may contain immediate operation acknowledgment, one terminal success or failure, requested status, bounded configured `status` heartbeats, `G73` threshold events, and unsolicited hard faults. High-rate control diagnostics use triggered, timestamped ESP-side buffering and may transfer only when transfer cannot disturb active control.
- Converge the production command contract on `docs/plan/ligature.md`, while keeping its explicitly unconfirmed `G73` parameter set and bottom-soft-limit exception open for the relevant decision tickets. Command details may change only through those decisions, not by accidental drift.
- Missing or invalid calibration should fail closed if a sufficiently simple validity scheme can achieve it; commissioning must remain possible without silently granting production motion.

## Decisions so far

- [Establish current SimpleFOC commissioning-tool options](issues/01-establish-simplefoc-commissioning-tool-options.md) — Evaluate WebController through a firmware-owned bounded Commander adapter; keep it development-only and retain ESP-side capture for timing evidence.
- [Establish SimpleFOC startup, Hall, and current-sense constraints](issues/02-establish-simplefoc-startup-hall-and-current-sense-constraints.md) — Stored electrical/current-sense calibration can avoid alignment, but startup PWM state, blocking commissioning, Hall timing, and board current metrology require explicit fixes and tests.
- [Inventory the lift-axis hardware and test safeguards](issues/03-inventory-lift-axis-hardware-and-test-safeguards.md) — Use only Motor 1 for standalone unloaded testing; Hall inputs are validated, bridge/current behavior is not, and the reachable mains plug is the physical power cut-off.
- [Define the simple unloaded-motor powered bring-up and smoke test](issues/04-define-safe-powered-bring-up-and-motor-test-ladder.md) — Accept bridge/motor loss risk and use one fixed, two-command unloaded diagnostic rather than a multi-gate qualification ladder.
- [Choose the commissioning and tuning workflow](issues/05-choose-commissioning-and-tuning-workflow.md) — Use stock WebController through explicit ordinary/raw firmware gates, minimal immutable motion/electrical ceilings, promoted JSON/NVS profiles, and control-priority serial servicing in one supervised acceptance procedure.
- [Implement and run the simple unloaded-motor smoke test](issues/14-execute-safe-unloaded-motor-powered-bring-up.md) — Motor 1 passed the bounded 0.9 V bidirectional unloaded smoke test with opposite legal Hall sequences, passive stops, nominal 1.571 A peak, and final PWM off; modest roughness is deferred to commissioning rather than treated as production validation.
- [Choose the control architecture and travel-fault criteria](issues/06-choose-control-architecture-and-travel-fault-criteria.md) — Use position/velocity/measured-current cascade control, simple target settling and progress/deadline checks, explicit disarming failures, and commissioning-derived limits without added thermal or overspeed machinery.

## Not yet specified

- Long-duration soak duration and environmental/load corners. These depend on observed failure modes and the final production state machine.

## Out of scope

- Raspberry Pi production-client implementation, UI behavior, page-turn sequencing, cameras, pneumatics, and relay control. Only their ESP32 wire-contract expectations matter here.
- A general-purpose torque/compliant travel mode, including the proposed `M50`/`M51` interface in `docs/plan/ligature.md` §11. Dedicated touchdown and force-calibration behaviors cover the known contact-controlled cases.
- Implementation of the Raspberry Pi `foc_diag` utility described in `docs/plan/ligature.md` §16. This map covers only the ESP32 behavior that a supervised diagnostic or commissioning client depends on; the host utility belongs to the Raspberry Pi effort.
- A cross-controller vacuum-state interlock or notification protocol; it requires bookscanner orchestration beyond this ESP32-only destination.
- Hardware redesign, including adding a hardware emergency stop. Supervised powered tests must nevertheless provide an immediate physical means to remove motor power.
