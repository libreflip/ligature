# Define the production operation and fault state machine

Type: grilling
Status: resolved
Blocked by: 06, 07, 08, 10

## Question

What explicit Idle, commissioning/raw-commissioning, Armed, Homing, Moving, Probing, Turning, Holding, and Fault states and transitions implement Ligature's lifecycle without implicit motion or automatic resume? Specify one active operation, busy rejection, acknowledgment plus one terminal result, hold gating, calibration/homing gates, emergency `M112` preemption with disarm/Fault latch/homing invalidation and `M999` recovery, routine `M53` preemption that remains armed/homed, disconnect/reset behavior, and which status operations remain available in every state.

## Answer

Use one closed sum-type state machine. Do not pair a coarse state with separate `armed`, `homed`/position-trusted, fault, exclusive-command, or positional-limit-override booleans. The state variants are:

- `CommissioningOnly`: compiled configuration or boot ADC-offset validation failed; production arming is unavailable.
- `IdleUntrusted` and `IdleTrusted`: zero PWM and disarmed, with position trust encoded by the variant.
- `ArmedUntrusted`: armed only for homing or the positional-limit override.
- `ArmedOverridePending`: armed and untrusted, with exactly one relative travel authorization pending.
- `Ready`: armed and position-trusted, with no exclusive command running.
- `AligningCommissioningOnly` and `AligningConfigured`, both externally reportable as `Aligning`.
- `Homing`, `Calibrating`, `Moving`, `MovingUntrusted`, and `TouchingDown`: exclusive-command states. `Moving`, `Calibrating`, and `TouchingDown` inherently carry trusted position; `MovingUntrusted` is solely the consumed positional-limit override.
- `Holding`: the settled press-current hold after touchdown has already reported success; it is not an exclusive command.
- `FaultUntrusted` and `FaultTrusted`.

There is no commissioning/raw-commissioning session state or client-specific Studio state. There is no `Turning` state for now: ordinary host-issued positional commands use `Moving`. Whether a future composite `G73` command and corresponding state should exist remains for the production-protocol decision.

### Boot, alignment, and ordinary progression

Boot or reset validates the compiled commissioned configuration and measures zero-current ADC offsets with PWM at zero. Success enters `IdleUntrusted`; failure enters `CommissioningOnly`. Reset from any state cancels all work, clears volatile runtime changes and any pending override, commands zero PWM, and invalidates position trust. A serial disconnect or silence that does not reset the ESP32 changes no state; bounded exclusive commands continue normally and `Holding` persists. Reconnection never starts or resumes motion.

Electrical alignment occurs only through an explicit supervised `M40`, never at boot, arm, or homing. It uses the bounded alignment procedure, produces candidate values in RAM, ends with zero PWM, and invalidates position trust. Alignment from `CommissioningOnly` returns there because volatile candidates cannot validate the compiled configuration; alignment from a configured idle state returns to `IdleUntrusted`. Normal production startup uses the compiled electrical calibration without moving. Production acceptance must physically verify that compiled calibration repeatedly starts reliably across cold boots and resets.

`M3` transitions `IdleUntrusted` to `ArmedUntrusted` and `IdleTrusted` to `Ready`. It is an idempotent no-op in `ArmedUntrusted`, `Ready`, and `Holding`; it remains busy during an exclusive command, faulted in a fault state, and unavailable in `CommissioningOnly`. Entering an armed stable state initializes angle control at the measured shaft angle and resets relevant controller integrators before enabling PWM, so arming cannot reuse a stale target or intentionally move.

`G28` may start from `ArmedUntrusted` or `Ready`. Success enters `Ready`; ordinary failure enters `IdleUntrusted`. `Ready` may start ordinary travel, touchdown, assembled-axis distance calibration, or bounded direct Studio control. Successful travel or calibration returns to `Ready`; travel holds its achieved target. Successful touchdown enters `Holding`, and `M24` transfers atomically to angle control at the measured release angle and enters `Ready`.

### Positional-limit override

The permanent one-command override is represented without a pending flag. Its command transitions `ArmedUntrusted` to `ArmedOverridePending`. That state accepts exactly one bounded relative travel command, regardless of whether it comes from the production client or Studio's command terminal. An accepted move consumes the authorization and enters `MovingUntrusted`; a rejected move does not consume it. Reissuing the override may succeed idempotently.

Successful overridden travel returns to `ArmedUntrusted` while holding the measured final shaft angle. `M53` returns there while holding the measured stop angle. Ordinary failure or `M5` enters `IdleUntrusted`; `M112`, a hard fault, or reset clears the authorization and cannot preserve trust. The stock Studio target widget does not become an open-ended untrusted-motion session: Studio must issue the same explicit relative command, and every later move needs another override.

### Exclusivity and command availability

An **exclusive command** is represented by its state; there is no active-operation slot. Alignment, homing, moving calibration, travel, overridden travel, and touchdown search are exclusive. While one runs, accept only `?`, `M155`, `M53`, and `M112`; reject every other production or Commander/Studio command immediately as busy, and queue nothing. Serial parsing remains incremental so control and immediate stop keep priority.

Stable-state permissions are:

- `CommissioningOnly`: explicit bounded alignment/commissioning controls, status, heartbeat configuration, and `M112`; no production arm.
- Configured idle states: `M3`, alignment, bounded parameter inspection/change, status, heartbeat configuration, and `M112`.
- `ArmedUntrusted`: homing, the positional-limit override, `M5`, status, heartbeat configuration, and `M112`.
- `ArmedOverridePending`: its relative move, repeated override, `M5`, status, heartbeat configuration, and `M112`.
- `Ready`: homing, ordinary movement, touchdown, distance calibration, `M5`, `M53`, bounded Studio commands, status, heartbeat configuration, and `M112`.
- `Holding`: `M24`, `M5`, `M53`, idempotent `M3`, `?`, `M155`, `M112`, and bounded finite parameter reads/writes applied atomically. Reject targets, mode or enable changes, alignment/reinitialization, and new production motion/calibration. Parameter writes are deliberately allowed even though changing gains or limits may alter live press behavior.
- Fault states: `?`, `M155`, `M999`, and idempotent `M112` only.

`M5` is idempotent in either idle state. From `ArmedUntrusted` or `ArmedOverridePending` it enters `IdleUntrusted`; from `Ready` or `Holding` it enters `IdleTrusted`. It commands zero PWM, clears stale targets, and disarms. It is busy during an exclusive command, so only `M53` routinely cancels one.

`M53` immediately cancels an exclusive command. From trusted `Moving` or `TouchingDown` it enters `Ready`; from `MovingUntrusted` it enters `ArmedUntrusted`; from `Homing` it enters `ArmedUntrusted`; from configured alignment it enters `IdleUntrusted`; from commissioning-only alignment it returns to `CommissioningOnly`; and from `Calibrating` it discards the candidate and enters `Ready` if Hall tracking remains valid. It also works in `Ready` to replace a direct Studio target with the measured shaft angle, and in `Holding` as an aborted rather than normal release. In both cases it enters or remains `Ready`. Every armed destination initializes angle control at the measured stop angle and resets stale controller history. Outside those states, `M53` is rejected because there is nothing to stop.

`M24` is valid only in `Holding`. `M999` transitions `FaultUntrusted` to `IdleUntrusted` and `FaultTrusted` to `IdleTrusted`; outside fault it is an idempotent no-op. State-inappropriate or rejected commands never alter state or controller targets.

### Failure and emergency behavior

Use three failure tiers:

1. Validation or state rejection causes no transition.
2. An ordinary exclusive-command failure commands zero PWM, emits terminal failure, and disarms without latching fault. It preserves trust only when the failed operation began trusted and Hall observation remained valid; failed homing and untrusted travel remain untrusted.
3. A hard fault or `M112` commands zero PWM immediately, clears stale targets and pending override, cancels any exclusive command or hold, disarms, and latches the appropriate fault variant.

Unexpected endstop contact, invalid/discontinuous Hall feedback, and interrupted homing produce `FaultUntrusted`. `M112` and current/control faults preserve trust when continuous legal Hall observation still supports it; otherwise they produce `FaultUntrusted`. Clearing fault never arms or initiates motion.

A preempting `M53` or `M112` emits one terminal response, not separate terminal lines for the cancelled command and stop command. That response identifies the cancelled command and deterministically retires both obligations. An unsolicited hard-fault line likewise identifies any cancelled command and is the sole terminal notification. Exact framing belongs to the production-protocol decision.

Status and heartbeat reporting derive trust from the state variants; do not maintain or expose a separate `Homed` boolean. Operation states inherently communicate their trust category. Exact external state names and fields remain for the protocol decision.
