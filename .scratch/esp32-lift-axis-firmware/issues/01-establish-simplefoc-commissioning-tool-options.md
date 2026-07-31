# Establish current SimpleFOC commissioning-tool options

Type: research
Status: resolved
Research context: change `llnwxnvv` → [commissioning-tool research](../../../docs/research/simplefoc-commissioning-tools.md)

## Question

Which currently supported, first-party SimpleFOC tools and protocols can commission an ESP32 controller by inspecting telemetry and tuning bounded motion/current-loop parameters, and what transport, firmware integration, operating-system, persistence/export, licensing, and safety-gating constraints would each option impose on this project?

## Answer

Evaluate the official SimpleFOC WebController first, with Commander as the wire-level protocol and serial-terminal fallback, but expose neither the stock full-motor Commander callback nor an unrestricted GUI surface. A project-owned commissioning adapter must allowlist operations, enforce immutable current/voltage/velocity and parameter bounds, report accepted values, and reject enable, target, mode, alignment, characterization, and autotune actions unless the matching supervised commissioning state authorizes them.

Treat WebController monitoring as low-rate human-facing commissioning telemetry only. High-rate evidence remains a timestamped, preallocated ESP32 RAM capture drained only when transfer cannot disturb control. Opening, reconnecting, or losing the GUI must leave the controller motionless and disarmed. Accepted settings must be recorded and persisted by project-owned versioned tooling or firmware rather than trusted to the GUI.

SimpleFOC Studio is a secondary experiment, not the default: its current macOS distribution and generated 2.4.0 code are not trustworthy enough for the critical path. Defer PySimpleFOC/PacketCommander until repeatable scripted sweeps or richer export justify its unfinished dependencies. The first GUI integration should be a motionless, read-only compatibility and loop-timing test before any bounded parameter writes are enabled.

The supporting comparison, upstream-source evidence, safety traps, and acceptance checks are in the [commissioning-tool research](../../../docs/research/simplefoc-commissioning-tools.md). This is a documentation/source decision, not device validation.
