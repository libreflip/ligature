# Choose the commissioning and tuning workflow

Type: grilling
Status: resolved
Blocked by: 01, 02, 04

## Question

Which commissioning tool and firmware boundary should this project adopt, which parameters may it observe or adjust, how are unsafe values bounded, how are accepted settings exported or persisted, and how does the workflow guarantee that GUI traffic and diagnostic transfer cannot initiate unapproved motion or block the control loop?

## Answer

Adopt the stock SimpleFOC WebController over USB WebSerial as the first commissioning client, with Commander-compatible serial access as the fallback. Do not build or fork a commissioning GUI initially. WebController is the best current existing option found in the upstream review, but the ESP32 firmware—not the browser—owns every safety decision.

### Firmware boundary and session lifecycle

Expose a project-owned Commander adapter rather than the unrestricted stock full-motor callback. A WebController connection starts read-only and disarmed. Its integrated command prompt can enter an explicit commissioning session on the same serial connection, so no terminal-to-browser disconnect is required. Opening, reconnecting, or resetting the interface cannot enter commissioning or initiate motion.

An ordinary commissioning session provides broad motor control within a small permanent safety boundary. It may read all useful motor, controller, limit, fault, timing, and monitoring state and may adjust:

- closed-loop current, velocity, and angle modes;
- targets within the active limits and any required target envelope;
- PID gains, output ramps, and low-pass filters;
- current, voltage, and velocity limits below the immutable ceilings; and
- monitoring variables and rate.

Direct-voltage and open-loop modes remain unavailable. Every numeric write must be finite. An invalid or over-limit write is rejected rather than silently clamped, and the current accepted value is returned. Parameter changes are volatile, take effect atomically at a control-loop boundary, and may be applied live where the selected closed-loop mode supports them.

A deliberate, lingering raw commissioning mode may be entered by a separate command. It additionally authorizes SimpleFOC alignment, motor characterization, and current-loop autotune until explicit exit, immediate stop, fault, or reset. Raw mode is not a safety bypass: the immutable ceilings still apply. Final assembled-axis firmware may expose a special action only when software remains responsive to immediate stop. The already accepted unloaded-development exception may use a physically tested bounded blocking action with the reachable mains plug as abort, but that exception must not silently carry onto the assembled lift axis.

Immediate stop preempts pending GUI work, commands zero PWM, cancels motion, exits raw and ordinary commissioning, and disarms. Reset and fault do the same. Firmware should stop on a reliably detected serial/DTR disconnect; because stock WebController does not provide a dedicated heartbeat, every active commissioning motion also has a simple maximum-duration deadline as the fallback for a browser crash or undetectable disconnect.

### Minimal permanent limits

Keep the immutable checks intentionally small and maintainable:

- phase-current ceiling;
- internal voltage ceiling;
- velocity ceiling, because an unloaded motor can overspeed at modest current;
- maximum continuous-motion duration; and
- a mandatory target envelope when position control is used on the assembled lift axis.

A commissioning operator may choose narrower temporary limits but cannot raise these ceilings. Do not add acceleration limits, per-gain tables, or elaborate parameter policy unless physical evidence shows another uncontrolled hazard. Exact production numeric values remain a later characterization decision.

### Profiles and persistence

Treat edits as volatile until an explicit, disarmed promotion. `PROFILE PRINT` emits a canonical, versioned JSON tuning profile for capture in a human-reviewable repository file; `PROFILE SAVE` writes the same accepted values to ESP32 NVS. The profile records the tunable controller values, limits, hardware identity, and firmware/library versions, but never target, enabled state, commissioning/raw state, or pending actions. It is distinct from the electrical-calibration record. Loading and compatibility rules belong to the calibration-validity decision, and loaded values can never override firmware ceilings.

### Control-loop and telemetry behavior

Motor control always wins over GUI responsiveness. Service a bounded amount of serial input only after control work, apply accepted writes at a loop boundary, and shed or stop GUI telemetry if timing pressure appears. Persistent control-deadline failure faults to zero PWM. WebController monitoring is low-rate human-facing telemetry; diagnostic evidence remains timestamped in a preallocated ESP32 buffer and is transferred only when transfer cannot disturb control.

Do not prescribe a fixed GUI rate before measurement. Start conservatively, then use the highest rate that preserves the measured control-loop and Hall-service budgets; more resolution is desirable only inside that bound.

Use one supervised end-to-end commissioning acceptance procedure rather than separate compatibility and powered-integration stages. In that procedure, connect WebController and check discovery, filtering, profile output, reset/reconnect behavior, and timing before unlocking powered controls in the same run. Any failed motionless gate ends that run before motion. The procedure then exercises powered tuning under the established ceilings and records the maximum acceptable telemetry rate and stop/disconnect behavior.

The glossary additions for **commissioning session**, **raw commissioning mode**, and **tuning profile** are recorded in [`CONTEXT.md`](../../../CONTEXT.md).
