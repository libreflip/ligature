# Define the production protocol and observability contract

Type: grilling
Status: resolved
Blocked by: 05, 09, 10

## Question

What exact 115200-baud line contract should Ligature expose for `M3`/`M5`, `M112`/`M999`, routine `M53`, the one-command positional-limit override and its relative move, `G28`, `G0`/`G1`, `G30`/`M24`, `M40`/`M41`, `?`, and `M155`? Decide whether firmware should expose a composite `G73` page-turn command and `Turning` state at all or leave every phase as a Raspberry-Pi-sequenced ordinary move; if retained, resolve the proposed `G73 U/R/B/C/Z` shape and blower events. Resolve the draft's internal contradiction around reserving `S` while using `M155 S`. Define structural arm-command unambiguity, parsing/validation and soft-limit errors, single-terminal preemption lines that identify the cancelled command, state-derived position-trust reporting without a separate `Homed` boolean, status and `status` heartbeat fields/rates, unsolicited faults, bounded diagnostic-capture transfer, and host-side conformance tests without blocking active control.

## Answer

Use a strict newline-terminated production dialect at 115200 baud alongside a separate allowlisted native SimpleFOC Studio/Commander dialect. The production contract is complete without `G73`, blower events, or a `Turning` state. Ordinary host-sequenced commands must support the entire current workflow. Whether to add a composite operation later is deferred to [Decide whether to add a composite page-turn operation](18-decide-whether-to-add-composite-page-turn-operation.md), and adding it must not make ordinary commands incomplete.

### Production grammar and dispatch

A production line is at most 96 bytes excluding its terminator, contains printable ASCII only, and ends in LF; an optional immediately preceding CR is ignored. Commands and parameter letters are uppercase. The form is `COMMAND [PARAM...]`, with whitespace separating parameters. A parameter is one uppercase letter followed by a finite fixed-decimal number; exponent notation, comments, checksums, concatenated commands, lowercase aliases, duplicate parameters, unknown parameters, missing required parameters, and trailing material are rejected atomically. Overlong or non-ASCII input is discarded through the next LF before one error is emitted.

`M3` arms only when the complete production command token is exactly `M3` and it has no parameters. Prefixes, substrings, malformed lines, and Commander traffic cannot arm. Dispatch exact production syntax first, then recognized allowlisted Studio syntax; reject anything matching neither parser. A production-looking malformed line is never reinterpreted as Studio input.

Studio cannot be configured to speak this production grammar, so retain its native compact Commander lines and native replies behind a firmware-owned adapter. Reads and writes are allowlisted, finite, bounded, and checked against the operation state and immutable ceilings. Studio `ME1` cannot bypass production arming: the operator issues exact `M3` in Studio's terminal before motion-capable Studio controls. Studio targets are accepted only in `READY`; untrusted relative movement still requires the production `M52` and `G1 D...` path. Exact `M112` always preempts Studio-controlled motion. The first valid production command disables ongoing Studio monitoring before its response, preventing native Studio telemetry from contaminating the production stream. Accepted volatile Studio changes remain active until reset.

The stale statement that `S` is never used is removed. Parameter letters are command-local, and `S` is used by `M155` for seconds.

### Response lifecycle

An accepted exclusive operation emits `ok <COMMAND>` immediately, then exactly one `done <COMMAND> ...` or `error <COMMAND> REASON:<TOKEN> ...` when it terminates. Exclusive operations include alignment, homing, calibration movement, travel, and touchdown. Immediate commands emit only their terminal `done` or `error` line. Rejected commands emit only `error`; no sequence identifiers are used because one exclusive operation exists at most and nothing queues. `?`, heartbeats, capture samples, and unsolicited hard faults use their own prefixes.

Validation order is deterministic: line encoding and lexical grammar; command and parameter-schema recognition; current-state permission; operation prerequisites; then numeric ranges and geometric safety. Canonical rejection reasons are `LINE_TOO_LONG`, `NON_ASCII`, `SYNTAX`, `UNKNOWN_COMMAND`, `MISSING_PARAM`, `DUPLICATE_PARAM`, `INVALID_PARAM`, `OUT_OF_RANGE`, `BUSY`, `FAULTED`, `CONFIG_INVALID`, `NOT_ARMED`, `POSITION_UNTRUSTED`, `OVERRIDE_REQUIRED`, `SOFT_LIMIT`, `ENDSTOP_ACTIVE`, `NOT_HOLDING`, and `NO_ACTIVE_OPERATION`. Runtime terminal reasons include `HOMING_FAILED`, `MOVE_FAILED`, `TOUCHDOWN_FAILED`, `ALIGNMENT_FAILED`, and `CALIBRATION_FAILED`. A soft-limit rejection identifies the target and inclusive minimum and maximum; it never clamps or moves.

Successful stable-state commands report resulting `STATE` and `TRUST`. Homing reports its actual final pull-off position, normally `Z:-2.000`; travel reports final `Z`; touchdown reports settled `Z` and commanded normalized `PRESS`; `M24` reports release `Z`. `M40` reports the volatile electrical-zero, sensor-direction, and current-sense mapping/sign candidates needed for the commissioned configuration. `M41 R...` reports actual shaft travel, while `M41 Z...` reports the volatile `MM_PER_REV` candidate. `M155` echoes its accepted `S` value.

Use fixed-decimal output: millimetres, millimetres per second/minute, and amperes to three decimal places; press percent to one; seconds to three; calibration constants with six where needed. Integer identifiers and timestamps remain integers. A physically unavailable value is `?`; firmware never emits NaN or infinity.

### Commands

The production command set is:

- `M3`, `M5`, `M112`, `M999`, `M53`, `M52`, `G28`, `M24`, `M40`, and `?`, all without parameters.
- `G0 Z<absolute_mm> [F<mm/min>]` and `G1 Z<absolute_mm> [F<mm/min>]` for trusted absolute travel.
- `G1 D<signed_relative_mm> [F<mm/min>]` as the only relative form, accepted only while the one-command authorization from `M52` is pending. Acceptance consumes the authorization; rejection does not. `Z` never changes meaning according to state.
- `G30 [P<press_percent>]`, defaulting to the commissioned press level. `P` deliberately denotes normalized press level rather than obsolete calibrated torque or force.
- `M41 R<signed_motor_revolutions>` for the exclusive measured calibration movement, followed by immediate `M41 Z<signed_measured_mm>` to calculate a volatile conversion candidate.
- `M155 S<seconds>`, where `S0` disables heartbeats and nonzero values from 0.200 through 60.000 seconds are accepted.

The permissions, state transitions, idempotence, trust effects, ordinary-failure behavior, and hard-fault behavior are those in [Define the production operation and fault state machine](09-define-production-operation-and-fault-state-machine.md). In particular, `M5` does not replace `M53` during an exclusive operation, and `M40`, homing, and all movement remain explicit.

### Preemption and unsolicited faults

`M53` and `M112` each produce one terminal line that retires both the immediate command and any cancelled operation; the cancelled operation emits no second terminal. Examples are `done M53 CANCELLED:G1 Z:-42.125 STATE:READY TRUST:1` and `done M112 CANCELLED:G30 STATE:FAULT TRUST:1`. Where no cancellation is meaningful, use `CANCELLED:NONE` or the state-defined `NO_ACTIVE_OPERATION` rejection.

A hard fault emits exactly one unsolicited line such as `fault CURRENT_LIMIT CANCELLED:G1 STATE:FAULT TRUST:1 Z:-42.125`. It is the sole terminal notification for a cancelled operation. Fault lines always identify the reason, cancelled command or `NONE`, resulting `FAULT` state, and state-derived trust; optional measurements provide evidence but do not alter framing. Production emits no unbounded debug logs.

### State and telemetry

Expose stable semantic state names rather than every internal sum-type variant: `COMMISSIONING_ONLY`, `IDLE`, `ARMED`, `OVERRIDE_PENDING`, `READY`, `ALIGNING`, `HOMING`, `CALIBRATING`, `MOVING`, `TOUCHING_DOWN`, `HOLDING`, and `FAULT`. `TRUST:<0|1>` is derived from the internal state variant; there is no stored or reported `Homed` boolean. `MOVING` and `FAULT` may therefore carry either trust value.

`?` emits one unframed full line:

`state STATE:... TRUST:... Z:... VEL:... IQ:... PRESS:... ENDSTOP:... PWM:... ACTIVE:... FAULT:... RUNTIME_MODIFIED:...`

Heartbeats emit the leaner line:

`status STATE:... TRUST:... Z:... VEL:... IQ:... PRESS:... ACTIVE:... FAULT:...`

`Z` is `?` whenever position is untrusted. `IQ` is measured q-axis current in amperes, never a torque or physical-force claim. `PRESS` is the commanded normalized level when applicable and otherwise `?`. Position and velocity are measured values, not targets. `ACTIVE` names the exclusive command or `NONE`; `FAULT` names the active hard-fault reason or `NONE`.

Heartbeats default off after reset. `M155 S0` disables them; accepted nonzero intervals are 0.2–60 seconds. They do not accelerate automatically during touchdown. Under transmit pressure a heartbeat may be coalesced or skipped rather than delaying control or a response.

### Diagnostic capture

`M120 N<samples> [D<loop_decimation>]` arms a bounded preallocated-RAM capture for the next exclusive operation. Capture records no live serial output. Samples include index, integer timestamp, state, position, shaft angle, velocity, target and measured q-axis current, q-axis voltage, Hall state, and endstop state. The compiled sample-count and decimation bounds protect memory and control timing.

`M121` transfers a completed capture only with PWM off and no active operation. It emits `ok M121`, bounded `capture ...` sample lines, then `done M121 COUNT:<n>`. `M122` clears an armed or completed capture. Capture survives disarm, abort, and fault for later retrieval, but not reset. A new capture cannot silently replace an armed or completed one.

### Scheduling and offline conformance

Keep serial integration allocation-free and incremental with fixed buffers. Each loop services Hall observation, endstop handling, control work, and pending zero-PWM effects before a bounded amount of serial work. Completed `M112` and `M53` lines take priority over ordinary buffered lines. Required responses are never dropped; heartbeat shedding happens first. Exact byte budgets and stop latency are measured implementation and device-acceptance values, not guessed protocol constants.

Put deterministic behavior in a platform-independent C++ protocol/state-machine module. Its small interface accepts time and plain hardware observations and returns explicit effects such as controller mode/target changes, zero PWM, capture actions, and output lines. It owns parsing, validation, state transitions, deadlines, and response generation without Arduino, ESP32, Serial, or SimpleFOC dependencies. The ESP32 adapter connects that interface to real hardware; a host simulation adapter drives the same interface with scripted time and observations.

A standalone host conformance harness compiles this exact module with an ordinary host C++ compiler and verifies strict and malformed parsing, every command's accepted and rejected states, acknowledgment/terminal ordering, heartbeat and fault interleaving, `M53`/`M112` cancellation, reconnect without implicit motion, Studio lines never becoming production commands, and bounded capture transfer. Separate on-device serial tests validate transport scheduling and latency, and separate physical procedures validate motor, Hall, current, endstop, homing, touchdown, and hold behavior. Offline conformance is never physical validation.
