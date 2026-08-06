# Ligature — MKS ESP32 FOC Board Firmware Specification

> Target-state spec for the firmware on the MKS ESP32 FOC board
> (hardware revision **V1.0**) driving the suction-box lift motor.
> Self-contained — everything needed to implement and test this is
> here; `reference/` files are supplementary hardware detail only
> (exhaustive tables, open hardware-verification items).
>
> Language/toolchain: C++ (Arduino framework) via **PlatformIO**, built
> on **SimpleFOC**, running on an ESP32-WROOM-32D. Connects to the
> Raspberry Pi via direct USB serial — no other device on this link.

---

## 1. Role

The board is **dumb**: it never decides *when* to move. It receives
G-code-style text commands over USB-serial from the Raspberry Pi,
executes exactly what each says, and reports back. All process logic
(when to touch down, when a page has separated, when to abort) lives on
the Raspberry Pi (`sans-serif.md`). This board's only job: move the
suction-box lift motor precisely, safely, and predictably, and report
what happened.

Everything is closed-loop: every commanded motion completes with an
explicit report (position reached, contact detected, error) — never
fire-and-forget.

---

## 2. Hardware facts

| Parameter | Value |
|---|---|
| Motor | StepperOnline 42BSA78-24-01, 24V BLDC, Hall-sensor feedback |
| Pole pairs | 4 |
| Phase resistance | 1.03 Ω |
| Phase inductance | 0.00043 H |
| Supply voltage | 24 V |
| Rated current | 4.80 A — one of three ceilings for `motor.current_limit`, see next row |
| Board peak current | 6 A/channel; current-sense shunt confirmed 10mΩ (2026-08-04, part marking + V1.0 schematic agree), but amplifier gain is an open contradiction between two sources — chip marking read as INA181A2/50V/V, schematic labels the same parts INA181A1/20V/V. ADC-implied ceiling is therefore either ~3.3A or ~8.25A, not settled. `docs/hardware/electronics.md` §2.10 |
| Gearbox | 10:1 planetary, ~90–95% efficiency (unconfirmed) |
| Belt/pulley | T2.5 (2.5mm pitch), 20-tooth pulley → ≈5mm linear travel/motor-shaft-revolution, ≈0.796mm/radian. Confirmed part order (not yet mechanically installed); still a theoretical estimate pending calibration — derivation in §12.2 |
| Torque constant (Kt) | Not trustworthy from datasheet (~15% disagreement between estimates) — calibrate empirically as current→force, §12.1 |
| Sensor type | `HallSensor` |
| Motor channel | **Motor 1 confirmed driving the actual lift motor** (Motor 0 is the channel with the burnt driver, used only for the endstop connector), confirmed 2026-08-04 via the separate `motor-smoke` bring-up firmware — `docs/hardware/electronics.md` §2.10. Still keep configurable in this protocol firmware, don't hard-code — this finding isn't from the protocol firmware itself yet |
| Endstop | One, top of travel. Exact GPIO not yet finalized — keep configurable |

Full GPIO map, connector pinouts, wire colors, and the open
hardware-verification items (Hall A/B/C pin order, exact endstop pin,
which motor channel): `reference/esp32-foc-firmware-requirements.md`.

---

## 3. Safety requirements (binding)

1. **No motion at boot, reset, or reconnect.** PWM stays disabled until
   an explicit `M3` (§5).
2. **No motion resumes automatically after any reset** (fault,
   watchdog, brownout, USB reconnect) — nothing is remembered or
   retried across a reset.
3. **`initFOC()` and the calibration procedures (`M40`/`M41`/`M42`,
   §12.1) physically move the motor** — only ever on an explicit,
   distinct command, never automatically.
4. **The arm command must be structurally unambiguous** — noise or a
   stray byte on the line must never be interpretable as `M3` or any
   motion command.
5. **Software-only safeguard** — no hardware emergency-stop exists,
   don't assume one.
6. **Endstop hard-stop rule:** the endstop may only be touched during an
   active homing cycle (§6). Any other contact — touchdown, hold, any
   move, jog, or move-to-top's approach (§10, which is specifically
   designed to stop short of it) — is a hard fault: motor cut
   immediately (no decel ramp), an unsolicited `fault
   ENDSTOP_UNEXPECTED` is sent, and the same `Fault` latch as `M112`
   (point 9) applies. Stopping an already-commanded move on endstop
   contact is fine — allowed; nothing may ever *start* motion from an
   endstop condition alone.
7. **`motor.current_limit` ceiling:** never above the lowest of 4.80A
   (motor rating), 6A (board channel peak), and the ADC-implied bound —
   either ~3.3A or ~8.25A depending on an unresolved amplifier-gain
   question, see §2 and `docs/hardware/electronics.md` §2.10. Start low,
   raise only after hardware validation. Bring-up testing so far has
   stayed at ≤0.75A, under both figures.
8. **Current limiting must always be configured before arming — never
   an unbounded/default `current_limit`, at any bring-up stage.** Two
   mechanisms, used in sequence, not interchangeably:
   - **Model-based (sensorless)** — bounded from phase resistance
     (1.03Ω) and back-EMF constant (4.41 V/krpm) alone, no current
     sense needed. Protects the first `initFOC()` alignment and any
     open-loop jogging before closed-loop sensing is verified — must be
     configured in firmware boot, before `M3` ever arms.
   - **Closed-loop measured** (`TorqueControlType::foc_current`, the
     INA181-family current sense — shunt confirmed 10mΩ 2026-08-04, but the
     amplifier gain (INA181A1/20V/V vs. INA181A2/50V/V) is an unresolved
     contradiction between the chip marking and the V1.0 schematic,
     `docs/hardware/electronics.md` §2.10) — only meaningful, and only
     enabled, after
     `initFOC()` has already succeeded once under model-based limiting:
     current-vector control needs a known electrical angle, which only
     exists post-alignment. Staged bring-up sequence:
     `ligature-AGENTS.md`.
9. **Soft limits, both ends of travel, added 2026-08-02.** Two
   configurable Z boundaries, independent of the physical endstop:
   - **Top soft limit** — sits a small, configurable margin below the
     physical endstop's Z position (the endstop itself is the hard
     limit; the soft limit exists so normal operation never gets close
     enough to depend on the endstop as a backstop). No command may
     target a Z less than this limit — **reject the command outright,
     don't execute it, don't clamp/substitute a fallback position** —
     `error SOFT_LIMIT` (§4.3). **Exception: `G28` homing's seek phase
     (§6) is explicitly allowed to drive past it** — finding the real
     endstop is the entire point of that phase, so the soft limit
     cannot apply there.
   - **Bottom soft limit** — a configurable maximum Z, a backstop
     against driving the mechanism into/past its physical bottom of
     travel if contact-detection somehow fails. Same reject-outright
     behavior for any command targeting past it. **Exception: `G30`
     touchdown's contact-seeking descent (§8) is allowed to drive past
     it**, symmetric with `G28`'s exception above and for the same
     reason — **proposed here, not yet confirmed with ijon/hrmny**,
     since it wasn't stated explicitly; without it `G30` could never
     reach a book thicker than the soft limit allows.
   - Both limits are persisted, configurable values (§4.3-style
     persistence), not hard-coded.
10. **All commanded motion accelerates/decelerates smoothly** — use
    SimpleFOC's built-in output-ramp rate limiting
    (`motor.P_angle.output_ramp` / the velocity-loop equivalent), not a
    custom trajectory planner; a simple ramp-rate limit satisfies this,
    full S-curve planning is not required. **Explicit exception: no
    deceleration ramp at the moment a stop-condition fires** — for
    `M112` (emergency stop), `G28` homing's endstop-triggered stop, and
    `G30` touchdown's contact-detected stop, the cut is immediate
    (already specified for `M112` and the endstop hard-stop rule, point
    6). This exception applies only to that final stopping instant —
    the approach/seek motion leading up to it still ramps normally.
11. **`M112` (emergency stop) and `M53` (routine attempt-abort, §9) are
   distinct commands, not interchangeable.** `M112` disarms and latches
   `Fault` — blocks every command but `M999`/`?`, cleared only by
   `M999` then a fresh `M3`, regardless of what's sent or when. `M53`
   stops motion only, stays `Armed`/`Homed`, never requires `M999`. Use
   `M112` only for genuine emergencies (Stop-button, unsolicited
   faults); use `M53` for routine pickup-failure aborts.

---

## 4. Command/response protocol

Text-based, one command per line, newline-terminated ASCII, **115200
baud** (matches the Arduino relay board, `monospace.md`, for operator
consistency — not a SimpleFOC requirement). Built as an extension of
SimpleFOC's `Commander`, with an added framing layer so **unsolicited**
messages (§4.2) can reach the host without being asked — plain
`Commander` doesn't support that alone.

### 4.1 Response pattern

Every host line gets, in order: **`ok`** (received, syntactically valid
— execution starts immediately; no queue, a new motion command is only
accepted once the previous one finished or errored, except `M112` which
always preempts immediately), then exactly one of **`done <CODE>
[field=value ...]`** or **`error <REASON> [detail]`**.

### 4.2 Unsolicited messages

Arrive at any time, not in response to anything — the host's read loop
classifies every line by prefix (`fault `/`status `, everything else
answers whatever command is outstanding), the same pattern
`monospace.md` uses for its `PRESS ` prefix:

- `fault ENDSTOP_UNEXPECTED [pos=<mm>]` — §3.6, treat like a Stop.
- `fault <OTHER> [detail]` — any other hard fault (current-sense
  overrange, watchdog reset recovery).
- `status Z:<mm|?> VEL:<mm/s> TORQUE:<force unit> STATE:<state>` — the
  heartbeat, §12.2. Continuous and automatic, no start/stop command,
  interval set by `M155` (§5). Rate increases automatically while
  `STATE` is `Probing` or `Turning` (§9). `VEL`/`TORQUE` are always
  actual measured values, never targets.
- `event BLOWER_ON`/`event BLOWER_OFF` — threshold-crossing reports
  during `G73` (§9), so the host doesn't have to compute page-width
  percentage crossings from raw position itself. Fired once each, at
  the moment the axis crosses the Z thresholds `G73`'s parameters
  specify — not repeated, not reversible mid-move.

### 4.3 Error reasons

| Reason | Meaning |
|---|---|
| `NOT_ARMED` | Motion command before `M3` |
| `NOT_HOMED` | Absolute-position command before a successful `G28` this session |
| `INVALID_PARAM` | Malformed/out-of-range parameter |
| `UNKNOWN_COMMAND` | Unrecognized line |
| `PICKUP_NOT_ARMED` | `M24` received while not in a touchdown hold |
| `ENDSTOP_FAULT` | See §3.6 |
| `FAULTED` | Anything but `M999`/`?` received while latched in `Fault` |
| `SOFT_LIMIT` | Target would cross a soft limit (§3.9) — command refused outright, nothing moves |

### 4.4 Parameter letters

| Letter | Meaning | Used by |
|---|---|---|
| `Z` | Absolute target position (mm) — the only axis this machine has; or a linear value in a calibration context | `G0`, `G1`, `G73`, `M41` |
| `U` | Absolute Z of `G73`'s first (upward, pickup-check) waypoint | `G73` |
| `F` | Feed rate, mm/min — speed only, never force | `G0`, `G1`, `G73` |
| `T` | Torque/current/force value (target, limit, or sample), calibrated force unit | `G30`, `M42`, `M51` |
| `R` | Retreat distance (`G73`) or revolution count (`M41`) — local meaning per command | `G73`, `M41` |
| `A` | Current (Amps), calibration-only | `M42` |

`S` is intentionally never used (real G-code: spindle speed, meaningless
here). Mode selection uses distinct command codes instead (`M50`/`M51`,
§11), same pattern as `M3`/`M5`.

---

## 5. Lifecycle & status

| Command | Meaning |
|---|---|
| `M3` | Arm — enables PWM, ready for motion commands. Required once per session. Doesn't home or move by itself. |
| `M5` | Disarm — disables PWM, returns to boot-idle. Safe anytime, always succeeds. |
| `M112` | **Genuine emergency stop only** (§3.11) — highest priority, cuts immediately (no decel), disarms, latches `Fault`. The interrupted command gets no `done`/`error` of its own. `done M112`. |
| `M999` | Clears `Fault` (harmless no-op otherwise). Does **not** re-arm or re-home — `M3` (and `G28`) still needed after. `done M999`. |
| `?` | Status query, no `ok`/`done` framing, replies immediately: `<STATE\|Z:<mm\|?>\|Vel:<mm/s>\|Torque:<force>\|Homed:<0\|1>\|Mode:<POS\|TORQUE>>`. `STATE` ∈ `Idle`, `Armed`, `Homing`, `Moving`, `Probing`, `Turning`, `Holding`, `Fault` — `Probing` is `G30`'s active contact-search and `Turning` is `G73`'s multi-phase move (§9), both distinct from a plain `Moving` positional move (`G0`/`G1`): `Probing` ends at a sensor-determined, not commanded, position; `Turning` needs the same tight telemetry timing `Probing` does, for the `event BLOWER_ON`/`OFF` crossings (§4.2). Works even while `Fault` is latched. |
| `M155 [S<seconds>]` | Configure `status` heartbeat interval (§4.2/§12.2); `S0` disables. Works in any state. `done M155 S<seconds>`. |

---

## 6. Homing

Establishes absolute zero **at the top of travel** (the endstop — the
book is at the *bottom*; moving toward the book is moving down,
increasing Z, moving away from it is moving up, decreasing Z). After
`G28`, `Z=0` is at/near the endstop; every other position is reached by
moving down from zero (fixed once, used consistently everywhere,
including the host's percentage-of-page-width math). Distinct from
move-to-top (§10) — different purpose/trigger, same physical endstop.

| Command | Meaning |
|---|---|
| `G28` | Home — requires `M3` first. **Its seek phase (below) is the one explicit exception to the top soft limit (§3.9)** — finding the real endstop requires being allowed to approach it. On success: `homed=true`, `done G28 Z0.0`. On failure (endstop never triggers within a sane bound): `error HOMING_FAILED`. |

**Sequence** (two-phase seek+locate, standard CNC practice — a single
fast touch isn't precise enough): (1) **seek** fast toward the endstop
to first trigger (overshoot expected); (2) **back off** a small
persisted pull-off distance until released, brief settling delay; (3)
**locate** slowly re-approach to a second, precise trigger — this
second touch is the recorded zero, not the first. Both touches are
legitimate, expected contacts (§3.6 doesn't apply to them).

`homed` clears on every boot, only set by a successful `G28` this
session — never assumed to survive a power cycle. Every
absolute-position command (`G0`, `G1`, `G73`, `G30`) is refused with
`NOT_HOMED` until `G28` succeeds. `G28` itself is only sent by the host
after explicit user confirmation — the host's/UI's responsibility, not
enforced here beyond requiring `M3` first.

---

## 7. Basic motion

All positions are **absolute mm on the Z axis, in the homed frame** —
this machine has exactly one axis, and it's Z (§4.4) — no
relative/incremental mode (`G90`/`G91` isn't needed; the host always
computes absolute targets itself). The board owns mm-per-revolution
internally (§12.2) — the host never sends raw angle/current in normal
operation. Every target is checked against both soft limits (§3.9)
before the move starts — a target past either one is refused with
`error SOFT_LIMIT`, not clamped.

| Command | Meaning |
|---|---|
| `G0 Z<mm> [F<mm/min>]` | Fast move, firmware's configured fast-travel speed (`F` overrides for this move only). `NOT_HOMED` unless homed. `done G0 Z<final_mm>`. |
| `G1 Z<mm> [F<mm/min>]` | Slow/controlled move — same gating, otherwise identical to `G0` but at the slow-travel speed (e.g. the pickup-check approach, §9). |

Any move can be preempted by `M112` (emergency) at any time; `G73`
specifically can also be preempted by `M53` (§9) — not interchangeable,
§3.11.

---

## 8. Touchdown and hold

**Scan photos are taken with the box down, compressed at a configured
press value, completely stationary** — the core safety/timing-critical
sequence.

| Command | Meaning |
|---|---|
| `G30 [T<press_value>]` | Touchdown — requires `M3`+homed. Descend (accelerating normally, §3.10) at the configured slow velocity, `current_limit` set to `T` (or the persisted default). **Allowed to cross the bottom soft limit** (§3.9) while probing — the whole point is reaching the book, which may sit past it. Detect contact via sustained velocity-sag-while-current-pinned (debounced — startup acceleration also briefly dips velocity; dwell time is a tunable persisted parameter). On detection: cut immediately, no decel ramp (§3.10), then hold fully stationary, no further motion, until `M24`. `done G30 Z<stop_mm> T<compression>` once the hold begins. |
| `M24` | Resume after hold — only valid while holding; releases it, the next move is now permitted. `error PICKUP_NOT_ARMED` if not holding. |

While holding, any command other than `M24`/`M112` is refused —
firmware-enforced, not caller discipline.

**Implementation note:** a move right after a hold that switches
`MotionControlType` to `angle` must initialize the angle target to the
motor's *current* `shaft_angle` first — otherwise it "unwinds" prior
rotation instead of moving relative to wherever touchdown actually
stopped (never the same position twice). Rely on SimpleFOC's built-in
anti-windup for the sustained current-saturation during the hold
(recurs every page) — don't reimplement it, don't reset PID state
incorrectly between holds.

---

## 9. Page-turn and abort

### `G73` — page-turn completion move (corrected sequence, 2026-08-02)

**`G73 U<mm> R<retreat_mm> B<mm> C<mm> Z<mm> [F<mm/min>]`** — proposed
parameter set, not yet confirmed with ijon/hrmny (5 Z-like values on
one line is a lot for this dialect's usual style — flagged for review,
not asserted as final). Sent right after `M24`. Three phases in one
call, not a single monotonic move — **an earlier version of this
document had the up/down order backwards; this is the corrected
sequence:**

1. **Up to `U`** (the pickup-check waypoint, e.g. 50% of page width —
   host-computed, all such percentages are configurable at the host/
   application level, not fixed here). **Pickup-detection happens
   during this phase, before the retreat below** — the host reads
   pressure independently (via `monospace.md`) and, on failure, cancels
   immediately with `M53` (below), never `M112`.
2. **Down by `R` mm** (the retreat/"wiggle," e.g. 10% of page width —
   independently configurable from `U`, not derived from it). This is
   the *only* downward motion this protocol permits with vacuum
   engaged. Only reached if phase 1 wasn't aborted.
3. **Up to the final target `Z`** (typically 105–120% of page width),
   continuing past `U` again. While crossing `B` and `C` en route,
   fires `event BLOWER_ON` / `event BLOWER_OFF` (§4.2) respectively —
   the host no longer has to compute these crossings from raw position
   itself. If `Z` would cross the top soft limit (§3.9), the command is
   refused upfront with `error SOFT_LIMIT` — **no silent fallback
   substitution**, unlike an earlier version of this document.
4. Stop fully stationary — `done G73 Z<final_mm>` — **not** a `Holding`
   state requiring `M24` (that's `G30`'s hold specifically, §8); the
   very next command (typically a fresh `G30`) works immediately.

This board still has no concept of pickup success — the check itself
lives entirely on the host side.

Requires `M3`+homed. Reports state `Turning` via `?`/`status` for the
whole 3-phase duration (§5, §12.2) — the host needs the same tight
telemetry timing during this as during `Probing`, to catch the `B`/`C`
crossings and the pressure-check window reliably.

### `M53` — abort current motion (feed hold)

**`M53`** — the **routine** way to abandon a failed pickup attempt, not
an emergency (§3.11). Stops whatever's moving, immediately, and nothing
else — same immediate-cut priority as `M112`, the interrupted command
gets no response of its own. `done M53 Z<stopped_at_mm>`.

Does **not** disarm, clear homed state, or fault — the next command
(typically a fresh `G30`) works immediately. Requires `M3` armed only
(no homing precondition — stopping doesn't need to know an absolute
position).

---

## 10. Move-to-top

**No dedicated command** — a plain `G0` to a small absolute Z near `0`
(up, away from the endstop — §6's coordinate convention). No separate
persisted margin; the host supplies `Z` directly, same as any other
`G0` — and like any `G0`, it's checked against the top soft limit
(§3.9) before moving, so a target that's too close to the endstop is
refused with `error SOFT_LIMIT` rather than silently attempted. In
practice the host should pick a `Z` at or just past the soft limit
itself, not closer.

Used for both the touchscreen recovery button and end-of-job parking
before `M5`. Requires homed (same generic gating as any absolute move).
Available anytime armed, including right after an `M112` stop or
mid-sequence abort. If it ever triggers the endstop, that's a hard
fault (§3.6), not a successful arrival.

---

## 11. Drive mode (proposal, not yet confirmed with ijon/hrmny)

| Command | Meaning |
|---|---|
| `M50` | Position-based (default after `G28`). `G0`/`G1`/`G73` use SimpleFOC `angle` control — drives hard to the exact target regardless of resistance, up to `current_limit` as a safety cap only (not a target press value). |
| `M51 T<limit>` | Torque-based. Same mechanism as touchdown: `velocity` control with `current_limit` pinned at `T` — compliant, stops early on resistance, reports the position actually reached (not necessarily the target). |

Both stateful/modal (persist until the other is sent), same pattern as
`M3`/`M5` — avoids overloading a parameter letter as a mode selector.
Rationale: most of the flip cycle needs precise, unyielding positioning
(pickup/turn detection is pressure-based, not stall-based); torque mode
generalizes touchdown's already-proven compliant-stop mechanism for
future cases needing it elsewhere. `done G0`/`done G1` responses in
torque mode must report the position actually reached — a real
behavioral difference from position mode the host must distinguish (it
already knows which mode is active).

---

## 12. Calibration and telemetry

### 12.1 Calibration commands

All three run entirely as firmware, always user-triggered, physically
move the motor, and persist results on the ESP32 (flash/NVS) — never
re-derived on boot, never silently defaulted.

| Command | Meaning |
|---|---|
| `M40` | Sensor/phase alignment (`initFOC()`) — briefly moves the motor. `done M40` or `error <reason>`. |
| `M41 R<revolutions>` | Position calibration step 1 — rotates by the given revolutions at a safe speed; host/user measures the resulting physical displacement externally (e.g. calipers). `done M41`. |
| `M41 Z<measured_mm>` | Step 2 — reports the external measurement for the preceding `M41 R`; firmware computes/persists mm-per-revolution. `done M41 K<mm_per_rev>`. |
| `M42 A<amps>` | Force calibration sample — holds the given current against an external test load (scale, hung weight). `done M42` once stable. |
| `M42 T<measured_force>` | Reports the external force measurement for the preceding `M42 A`; accumulates `(current, force)` pairs, computes/persists a fit after ≥3 samples spanning the operating range. `done M42` (`FIT` once computed/updated). |

Curve-fit method (linear vs. piecewise) not fixed — a simple linear fit
is an acceptable start.

### 12.2 Telemetry — `?` and `status`

Two mechanisms: `?` (§5) is pull-based, answers once on request, with a
fuller field set (`Homed`/`Mode` beyond what the heartbeat carries).
`status` (§4.2) is push-based, continuous, leaner field set (bandwidth
cost is per-frame, repeated constantly, not once).

**Both are derived values, not raw SimpleFOC output** — the board
converts before anything reaches the wire (§13):

- **Position (mm):** `(shaft_angle_now − shaft_angle_at_G28) ×
  mm_per_radian`. Derivation of the constant: 20 teeth × 2.5mm pitch =
  50mm belt travel/pulley-revolution; ÷10 (gearbox) = 5mm/motor-shaft-
  revolution; ÷2π ≈ 0.796mm/radian. Still a theoretical estimate
  pending real calibration once mechanically installed (full sourcing
  of the pulley/belt part numbers: `reference/
  esp32-foc-firmware-requirements.md` §4.1). **Known resolution limit:**
  with 4 pole pairs and 3 Hall sensors, raw Hall transitions occur
  roughly every 0.2mm of linear travel at this gearing; finer
  resolution between transitions is SimpleFOC's velocity-based
  interpolation, not a direct measurement, least reliable exactly when
  velocity is changing fastest (i.e. at touchdown contact) — confirmed
  acceptable at ~0.2mm for this application (ijon, 2026-08-02).
- **Velocity (mm/s):** `shaft_velocity × mm_per_radian` — always the
  actual measured value, never a target, in both `?` and `status`.
- **Torque/force:** the FOC q-axis current (requires
  `TorqueControlType::foc_current`, i.e. real closed-loop current
  sensing, §3.8) run through the empirically-fitted current→force curve
  (§12.1) — the actual measured current translated to force, not the
  configured `current_limit`.

**`status` heartbeat rate:** configured base rate via `M155`, 1–5Hz is
the expected normal range. Automatically increases while `STATE` is
`Probing` (`G30`'s active contact-search) — the data needed to tune the
touchdown-detection dwell time and thresholds, the same role continuous
BMP180 streaming played for characterizing the pressure sensor
(`docs/hardware/bmp180-vacuum-drop-test.md`). Exact achievable rate
during `Probing` needs empirical measurement on real hardware (bounded
by the 115200 baud link). Reverts to the base rate automatically once
`Probing` ends — contact detected (→ `Holding`) or `M53` abort
(→ `Armed`) — no separate command needed to step the rate back down.

`G30`'s `done` additionally reports the stop position and
compression/press value at the moment of contact — this pairing is what
the host correlates with a separately-measured air-pressure reading for
diagnostics; must reflect the actual detected event, not a nominal
value.

---

## 13. Units

The host (`sans-serif.md`) only ever sends/receives **mm** for position
and the **calibrated force unit** (§12.1) for press values/current
limits — never raw radians or an uncalibrated Amps value in normal
(post-calibration) operation. The board owns all angle↔mm and
current↔force conversions internally, using the constants persisted by
§12.1's calibration commands. The host keeps no separate copy of these
constants to reconvert with.

---

## 14. Worked example: one full job

Illustrative only — exact values are placeholders, and the pickup-check/
Arduino-coordination detail belongs to `sans-serif.md` §5, not this
board.

Page width for this example: 148mm. Touchdown depth varies per
attempt/book, so `U`/`B`/`C`/`Z` below are illustrative absolute values
the host computed from *this* attempt's touchdown Z (200.0) and the
configured percentages (50%/95%/105%/110% of page width) — not fixed
numbers.

```
> M3                          arm the driver
< ok
< done M3

> G28                         home (only after UI confirm)
< ok
< done G28 Z0.0

  -- per page slot --
> G30                         touchdown (uses persisted default press)
< ok
< done G30 Z200.0 T2.1

  (capture_pair() happens on the host, outside this protocol)

> M24                         resume — box may move again
< ok
< done M24

> G73 U126.0 R14.8 B59.4 C44.6 Z37.2   up to the 50% check point, then
< ok                                    down 14.8mm (10%), then climb to
                                        110% — one call, sent right away

  -- outcome A: pickup confirmed during the up-to-U phase --
  (host reads pressure while the axis approaches U=126.0; sees a good
   seal, does nothing — G73 keeps running on its own)
< status Z:126.0 VEL:-8.2 TORQUE:1.9 STATE:Turning   (example heartbeat
                                                      frame, arrives on
                                                      its own throughout)
  (retreat to 140.8 happens automatically, no host action; climb
   resumes toward Z37.2)
< event BLOWER_ON                       fired crossing Z59.4
< event BLOWER_OFF                      fired crossing Z44.6
< done G73 Z37.2

  -- next page slot starts right here: --
> G30                         descends from Z37.2 until contact — this
< ok                          IS the "next downward motion towards the
< done G30 Z196.4 T2.0        book," from wherever G73 left the box

  -- outcome B: pickup failed during the up-to-U phase, host cancels --
> M53                         routine abort, NOT M112 (§3.11/§9) — sent
< ok                          before ever reaching U in this example
< done M53 Z168.5              (G73 itself never gets its own response;
                               box is just stopped, nothing else changes,
                               retreat/climb phases never happened)
  (host also switches off the separation fan and vacuum via monospace.md)
> G30                         fresh touchdown for the retry, sent by the
< ok                          host whenever it's ready — descends
< done G30 Z201.1 T2.1        correctly from wherever M53 left the box

  ... repeat ...

  -- job finished --
> G0 Z2.0                     move to top: small Z near the homed zero
< ok
< done G0 Z2.0

> M5                          end job, disarm
< ok
< done M5
```

---

## 15. Toolchain: PlatformIO

**Repo confirmed, 2026-08-04: `github.com/libreflip/ligature`** — built
directly on upstream SimpleFOC, no prior code extended (unlike
`monospace`). Currently holds bring-up/diagnostic firmware
(`hall-validation`, `simplefoc-hall-validation`, `motor-smoke`), not yet
the full protocol implementation below. hrmny's repo — see
`ligature-AGENTS.md`/`tasks.md` T32 for status and for the known,
deliberately deferred drift between that repo's own spec copy and this
document.

`platformio.ini`, translating `reference/esp32-foc-firmware-requirements.md`
§1.3's Arduino-IDE-equivalent settings into PlatformIO's own config
(PlatformIO's ESP32 defaults don't automatically match them):

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

; SimpleFOC — critical: without lib_archive = false, PlatformIO's default
; static-library archiving breaks SimpleFOC's compilation.
lib_deps = askuric/Simple FOC@^2.3.5
lib_archive = false

monitor_speed = 115200
upload_speed = 921600

; Translated from the Arduino IDE settings this board's docs assume
; (board "ESP32 Dev Module", flash mode QIO, flash size 4MB, partition
; scheme "Default 4MB with spiffs", PSRAM disabled). Verify against the
; installed espressif32 platform version — partition-table filenames
; have shifted across releases.
board_build.flash_mode = qio
board_build.partitions = default.csv
; No board_build.psram line needed — the WROOM-32D has no PSRAM at all.
```

Flash directly from the Raspberry Pi the board is connected to (avoids
a dev-laptop/Pi toolchain or serial-port discrepancy) — install
PlatformIO's CLI, not the desktop IDE.

**Likely ESP32 USB auto-reset on connect** (CP2102/CH340-family
USB-serial, same class of DTR/RTS circuit as the Arduino Uno) — treat
as likely true until confirmed on the real board; relevant to how the
diagnostic tool (§16) is designed.

---

## 16. RPi-side diagnostic tool (`foc_diag`)

A separate, purpose-built binary (`sans-core/src/bin/foc_diag.rs`,
alongside but independent of `hw_diag`) for manually jogging the motor,
triggering calibration (§12.1), and watching telemetry (§12.2) during
bring-up — not an extension of `hw_diag` (reasoning:
`architecture.md` AD-008).

This board's safety stakes are categorically higher than the relay
board's — `M3` arms a real motor, and this exact board already suffered
a hardware failure during an earlier bring-up attempt
(`docs/hardware/electronics.md` §2.4). Before this tool (or anything
else) ever sends `M3`: confirm current hardware status with ijon/hrmny
first — don't assume the machine is in a flashable/testable state just
because this document exists.

**Design requirements beyond a plain protocol client:**

- Same operating model as `hw_diag`: one persistent session per
  invocation (`foc_diag --port <esp32-port>`), `--port` required, never
  auto-detected (two independent USB-serial devices are typically
  present on the same Pi).
- **Explicit confirmation before arming (`M3`) and before any
  calibration/homing command** (`M40`–`M42`, `G28`) — print the
  currently configured `current_limit`/`voltage_limit` and require more
  than pressing Enter. Plain jogs once armed+homed don't need this same
  gate.
- **Surface `Fault` prominently and enforce the recovery gate at the
  tool level too** — visually distinguish it, refuse to forward
  anything but `?`/`M999` until acknowledged, then still require the
  normal separate `M3` afterward. Deliberate redundancy on top of the
  firmware's own enforcement.
- **Log by default**, not just on request (unlike `hw_diag`) —
  timestamped telemetry is what the calibration and touchdown-tuning
  work needs to review afterward.
- **A read-only/status-only mode** that never sends anything
  motion-capable — for confirming the board boots and reports sane
  telemetry before ever risking arming it.
- Calibration and homing commands physically move the motor — walk
  through them one at a time with the operator present and watching,
  regardless of what confirmation gates exist.

---

## 17. Bring-up status

**Repo:** `github.com/libreflip/ligature` — hrmny's repo, currently holds
bring-up/diagnostic firmware (`hall-validation`, `simplefoc-hall-validation`,
`motor-smoke` environments), not yet the full protocol implementation
specified above. Has its own `.scratch/`-based local issue tracker and a
copy of this spec at `docs/plan/ligature.md` — **that copy is an earlier
snapshot, not kept in sync** (hrmny started implementation before several
revisions made here). Known differences: `X` instead of `Z` for the
position parameter; only a single `M112` with no `Fault`-latch/`M999`
distinction from routine abort (no `M53`); no soft limits (§3.9); no
`status` heartbeat or `BLOWER_ON`/`BLOWER_OFF` events. **Per ijon
(2026-08-04): known, deliberately deferred — not a regression to fix
proactively.** Reconcile once the Stage 1b diagnosis below concludes;
don't touch the repo's copy unprompted.

**Bring-up sequence:** don't try to exercise the whole protocol at once.
Matches `reference/esp32-foc-firmware-requirements.md`'s own Milestone 1
(raw demo/jog) vs. Milestone 2 (full touchdown/homing/calibration) split,
refined into sub-stages given the hardware constraints hit during
bring-up. **Binding throughout every stage** (§3.8): current limiting
must be configured before the driver is ever armed — never boot into an
unbounded/default `current_limit`. Which *mechanism* provides that
limiting escalates stage by stage (sensorless/model-based first,
closed-loop measured only once earned); the requirement that *some*
limiting is always active does not.

- **Stage 1a** (old, damaged board; motor phase cables U/V/W not
  connected, encoder/Hall cable connected — motor cannot physically move
  regardless of firmware behavior, a real safety margin for these first
  tests) — **appears complete:** `M3`/`M5`/`M112`/`M999`/`?` confirmed
  working with sane telemetry and no motor circuit present; validated
  `hall-validation`/`simplefoc-hall-validation` firmware environments
  exist in the repo as evidence. Hand-rotating the shaft to sanity-check
  Hall wiring (zero risk, phases disconnected) and verifying the endstop
  triggers `fault ENDSTOP_UNEXPECTED` reliably are both free actions at
  this stage if not already done — don't skip them retroactively if
  picking this up mid-stream.
- **Stage 1b** (replacement board, U/V/W connected, motor still
  mechanically unmounted) — three sequential sub-steps, don't skip ahead:
  (1) model-based current limiting configured before arming, bounded from
  phase resistance (1.03Ω) and back-EMF constant (4.41 V/krpm, §2);
  (2) `M40`/`initFOC()`-equivalent alignment once under that limit, then
  short open-loop jogs at the same limit checking rotation
  direction/smoothness; (3) closed-loop measured current limiting brought
  up and cross-checked against the model estimate before trusting it,
  then raised gradually with telemetry watched at each step, bounded by
  §3.7's ceilings (4.80A motor / 6A board channel / ADC-implied,
  currently either ~3.3A or ~8.25A depending on an open amplifier-gain
  question, `docs/hardware/electronics.md` §2.10). **Status:** steps 1–2
  validated at 0.9V alignment voltage; step 3's current sensing works and
  gates correctly (amplifier gain still open, see above). All of this was
  done in a separate, purpose-built `motor-smoke` diagnostic firmware,
  **not** the real protocol firmware specified in §1–§16 above — no
  protocol-firmware implementation has started. That diagnostic firmware
  went further than this stage's own scope and surfaced a real,
  unresolved problem: closed-loop (Hall-velocity-feedback) motion at
  0.75A overspeeds, moves roughly, and fails its own stop-deceleration
  acceptance — suspected electrical-angle/commutation or coarse
  Hall-velocity-resolution issue (wiring already ruled out by a dedicated
  test). Full log: `docs/hardware/electronics.md` §2.10,
  `incoming/14-execute-safe-unloaded-motor-powered-bring-up.md`. **Don't
  start the real protocol firmware's closed-loop motion handling until
  this is resolved.**
- **Stage 2** (mechanically installed) — not started; blocked on Stage
  1b's resolution above and separately on the mechanical-integration
  blocker (`docs/hardware/electronics.md` §2.4). Once unblocked: `G28`
  becomes meaningful for real, then `G0`/`G1`, calibration
  (`M41`/`M42` — press-force limits derived from real calibration data,
  not just the safety ceiling), eventually `G30`/`G73`/`M53`. Current
  limiting stays on the closed-loop measured mechanism from Stage 1b
  throughout — don't drop back to model-based estimation once measured
  limiting has been verified. Don't attempt Stage 2 commands during Stage
  1a/1b — they either error out (`NOT_HOMED`) or would need a real
  mechanical reference that doesn't exist yet to mean anything.
- Not needed anywhere in Stage 1a/1b: `G28`, anything in the homed mm
  frame (`G0`/`G1`/`G73`/`G30`/`M53`), `M41`/`M42`, `M50`/`M51`.
- **Resolved (ijon, 2026-08-02):** no separate jog command — `G1` after a
  real `G28` is the jog mechanism whenever controlled-motion testing is
  needed; no homing-independent raw-jog command gets added to the
  protocol.

**This section is status, not spec** — unlike §1–§16 above, it's expected
to go stale and needs re-verifying each session, not treated as a fixed
requirement. `tasks.md` T32 tracks this same status at the project-task
level (scope/done-when/depends-on); update both together, this file is
the authoritative copy of the bring-up narrative itself since it's the
one that travels standalone with `ligature-AGENTS.md`.

---

## Reference

Exhaustive hardware tables (motor/board datasheet values, full GPIO
map, connector pinouts) and the still-open hardware-verification items
not resolvable without the physical board in hand:
[`reference/esp32-foc-firmware-requirements.md`](reference/esp32-foc-firmware-requirements.md).
