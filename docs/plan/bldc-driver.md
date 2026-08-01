# BLDC Driver — MKS ESP32 FOC Board Firmware Specification

> Target-state spec for the firmware running on the MKS ESP32 FOC board
> (hardware revision **V1.0**) that drives the suction-box lift motor.
> Self-contained: written to be handed to a firmware implementer with no
> other file access. Cross-references to `reference/` files below are for
> supplementary hardware detail only (exhaustive parameter tables, open
> hardware-verification items) — everything needed to implement and test
> the behavior specified here is contained in this document.
>
> Language/toolchain: C++ (Arduino framework) via **PlatformIO**, built on
> **SimpleFOC**. Runs on an ESP32-WROOM-32D.
>
> Connects to the Raspberry Pi via USB serial, direct — no other device is
> in this link.

---

## 1. Role

The board is **dumb**: it never decides *when* to move. It receives a
stream of G-code-style text commands over USB-serial from the Raspberry
Pi, executes exactly what each command says, and reports back. All
process logic (when to touch down, when a page has separated, when to
abort a job) lives on the Raspberry Pi (`sans-serif.md`). This board's
only job is to move the suction-box lift motor precisely, safely, and
predictably, and to tell the host what happened.

Everything is **closed-loop**: every commanded motion completes with an
explicit report of what actually happened (position reached, contact
detected, error), never a fire-and-forget.

---

## 2. Hardware facts the firmware must work against

| Parameter | Value |
|---|---|
| Motor | StepperOnline 42BSA78-24-01, 24V BLDC, Hall-sensor feedback |
| Pole pairs | 4 |
| Phase resistance | 1.03 Ω |
| Phase inductance | 0.00043 H |
| Supply voltage | 24 V |
| Rated current | 4.80 A — hard ceiling for `motor.current_limit` |
| Board peak current | 6 A per channel; current-sense ADC implies ~8.25A outer bound — treat ~8A as the tightest ceiling until validated |
| Gearbox | 10:1 planetary, ~90–95% efficiency (unconfirmed) |
| Belt/pulley | T2.5, 20-tooth pulley → ≈5mm linear travel per motor-shaft revolution (≈0.796mm/rad) — **theoretical estimate only, must be empirically calibrated**, see §7 |
| Torque constant (Kt) | Not trustworthy from datasheet (two estimates disagree ~15%) — must be calibrated empirically end-to-end as current→force, see §7 |
| Sensor type | `HallSensor` |
| Motor channel in use | Not yet confirmed (M0 or M1) — must stay configurable, not hard-coded |
| Endstop | One, at the **top** (upper end) of travel. Physical GPIO wiring not yet finalized — see `reference/esp32-foc-firmware-requirements.md` §1.5/§8 for the current wiring state. Firmware must treat the endstop GPIO as a configurable pin, not hard-coded, until that's resolved. |

Full electrical/GPIO detail, wire colors, connector pinout, and the
still-open hardware-verification items (Hall A/B/C pin order, exact
endstop pin, which motor channel): `reference/esp32-foc-firmware-requirements.md`.

The MKS ESP32 FOC V1.0 schematic has no separate motor-driver enable input;
GPIO 21 and GPIO 22 are unconnected. Throughout this specification, “PWM
disabled” means all three phase PWM commands for the channel are forced
low/zero and the motor is inactive in software. It does not remove bridge
power. Only disconnecting motor power provides physical isolation.

---

## 3. Safety requirements (binding, non-negotiable)

1. **No motion at boot, reset, or reconnect, ever.** Driver initializes
   with PWM disabled and stays disabled until an explicit host command
   arms it (`M3`, §5).
2. **No motion resumes automatically after any reset** (fault, watchdog,
   brownout, USB reconnect). The firmware never remembers or retries a
   move across a reset.
3. **Sensor-alignment (`initFOC()`) and the mm/force calibration
   procedures physically move the motor** — both only run on an explicit,
   distinct calibration command (`M40`/`M41`/`M42`, §8), never
   automatically.
4. **The arm/enable command must be structurally unambiguous.** Random
   noise or a stray byte on the serial line must never be interpretable
   as `M3` or any motion command.
5. **This is a software-only safeguard.** No hardware emergency-stop
   exists. Don't assume one.
6. **Endstop hard-stop rule:** the endstop must only ever be touched
   during an active homing cycle (§9). Triggering it at any other time —
   during touchdown, the photo-capture hold, any subsequent move, a jog,
   or the move-to-top approach (§10, which is specifically aimed to stop
   short of it) — is a **hard fault**:
   - All phase PWM commands go to zero immediately, not through a controlled
     deceleration. This is not bridge-power isolation.
   - An unsolicited `fault ENDSTOP_UNEXPECTED` message (§6) is sent to
     the host — never a silent stop.
   - Reading the endstop and *stopping* an already-commanded move is not
     "moving on its own" and is allowed; nothing may ever *start* motion
     from an endstop condition alone.
7. `motor.current_limit` must never be configured above the lowest of:
   4.80A (motor rating), 6A (board channel peak), ~8A (ADC-implied
   ceiling). Start low, raise only after hardware validation.

---

## 4. Command/response protocol

Text-based, one command per line, newline-terminated, ASCII. Built as an
extension of SimpleFOC's `Commander` (matches the vendor's own example
code as a starting point) with an added framing layer so **unsolicited**
messages (fault reports, §6) can be sent from firmware to host without
being asked — plain `Commander` request/response alone doesn't support
that and must be extended.

### 4.1 Response pattern

Every host-issued line gets, in order:

1. **`ok`** — the line was received and is syntactically valid; execution
   begins immediately (no command queue — a new motion command is only
   accepted once the previous one has completed or errored, except `M112`
   which always preempts immediately, §5).
2. Exactly one of:
   - **`done <CODE> [field=value ...]`** — the command completed
     successfully. Fields depend on the command, see each command's
     entry below.
   - **`error <REASON> [detail]`** — the command failed or was refused.

### 4.2 Unsolicited messages

Can arrive at any time, not in response to anything:

- **`fault ENDSTOP_UNEXPECTED [pos=<mm>]`** — §3.6. Host must treat this
  exactly like a stop condition.
- **`fault <OTHER> [detail]`** — any other firmware-detected hard fault
  (e.g. current-sense overrange, watchdog reset recovery).

### 4.3 Error reasons

| Reason | Meaning |
|---|---|
| `NOT_ARMED` | Motion command received before `M3` |
| `NOT_HOMED` | Absolute-position command received before a successful `G28` this session |
| `INVALID_PARAM` | Malformed or out-of-range parameter |
| `UNKNOWN_COMMAND` | Unrecognized line |
| `PICKUP_NOT_ARMED` | `M24` received while not in a touchdown hold |
| `ENDSTOP_FAULT` | See §3.6 |

### 4.4 Parameter letters

To avoid ambiguity with real-world G-code/CNC convention (where `F` and
`S` already have fixed, unrelated meanings):

| Letter | Meaning here | Used by |
|---|---|---|
| `X` | Absolute target position (mm), or a linear-quantity value in a calibration context | `G0`, `G1`, `G73`, `M41` |
| `F` | Feed rate / speed, **mm per minute — speed only, never torque or force** | `G0`, `G1`, `G73` (optional on all three) |
| `T` | Torque/current/force-domain value (target, limit, or measured sample), in the calibrated force unit from §8 | `G30`, `M42`, `M51` |
| `R` | Retreat distance (mm) for `G73`'s embedded downward step, or revolution count for `M41`'s step 1 — meaning is local to the command it appears on, following the same convention real G-code uses for canned-cycle `R` (retract plane) | `G73`, `M41` |
| `A` | Current (Amps), calibration-only | `M42` |

**`S` is intentionally never used.** In standard G-code it means spindle
speed (RPM); this machine has no spindle, and reusing the letter for
something else here would be more confusing than defining a new one.
Where a mode or state needs selecting (drive mode, positioning mode
elsewhere in real G-code), this dialect uses **distinct command codes**
instead of a parameter (see `M50`/`M51`, §11), the same pattern already
used for `M3`/`M5`.

---

## 5. Job lifecycle commands

| Command | Meaning |
|---|---|
| `M3` | **Start job** — arms active control, making it ready to accept motion commands. There is no separate hardware-enable pin. Required once per session before anything else moves. Does **not** by itself run homing or any motion. |
| `M5` | **End job** — disarms the driver (disables PWM), returns to the boot-time idle state. Safe to call any time; always succeeds. |
| `M112` | **Immediate stop.** Highest priority — processed even mid-move, ahead of anything else in flight. Commands all phase PWMs zero immediately, not a decel ramp; it does not isolate bridge power. Responds `done M112`. **The command it interrupted gets no `done`/`error` of its own** — `M112`'s response is the only one the host should expect for that in-flight command; don't wait for a second response that will never arrive. Does **not** disarm (`M3` state is retained — a subsequent motion command works without re-arming), unlike `M5`. Used both for genuine emergencies (Stop button, unsolicited fault) and for routine cases where the host decides mid-move that it wants to abort and retry (e.g. a failed pickup-success check during `G73`, §6.1) — the board treats both identically, it has no notion of "routine" vs. "emergency." |
| `?` | **Status query.** No `ok`/`done` framing — replies immediately, single line: `<STATE|Pos:<mm or "?">|Torque:<amps>|Homed:<0 or 1>>`. `STATE` ∈ `Idle`, `Armed`, `Moving`, `Holding`, `Homing`, `Fault`. Position is `?` until homed. Always answerable, regardless of armed/homed state. |

---

## 6. Motion commands

All position values are **absolute millimeters in the homed reference
frame** (established by `G28`, §9) — there is no relative/incremental
addressing mode in this protocol. The host always computes absolute mm
targets itself (it has everything needed to: `G30`'s reported touchdown
stop position for this attempt, plus the calibrated `page_width_mm` for
this book) and sends them directly. This deliberately drops the
`G90`/`G91` absolute/relative distinction real G-code has — it isn't
needed here, and every command the host actually sends
(`G28`, `G73`, `G0`, `G1`, `M3`, `M5`, ...) stays a flat, fixed-meaning
line.

The board owns the mm-per-revolution calibration (§8) internally; the
host never sends raw angle/current units in normal operation.

| Command | Meaning |
|---|---|
| `G0 X<mm> [F<mm/min>]` | **Fast move.** Rapid positioning at the firmware's configured fast-travel speed (persisted parameter, tunable; `F` overrides for this move only). Refused with `NOT_HOMED` unless homed. Responds `done G0 X<final_mm>`. |
| `G1 X<mm> [F<mm/min>]` | **Slow / controlled move.** Same as `G0` but at the firmware's configured slow/controlled speed. Use where the caller wants a more deliberate, lower-speed approach (e.g. the pickup-success check position). Refused with `NOT_HOMED` unless homed. Responds `done G1 X<final_mm>`. |

Every move in progress can be preempted by `M112` (§5) at any time.

### 6.1 `G73` — page-turn completion move

**`G73 X<mm> R<retreat_mm> [F<mm/min>]`**

Covers the **entire upward motion of a page-turn attempt**, sent
immediately once the host is ready to attempt the turn (right after
`M24`) — up to the given absolute target `X`, typically the
105–120%-of-page-width point the host has computed. It does **not**
include the following descent back toward the next page's touchdown —
that happens separately, via the *next* `G30` call (§7), which
naturally descends from wherever `G73` leaves the box.

**This board has no concept of "pickup success."** Whether the page was
actually picked up is something only the host can tell (it owns the
pressure sensor, not this board) — the host is expected to read
pressure on its own while `G73` is in flight and, if it decides the
pickup failed, cancel the move early with `M112` (§5) rather than
letting it run to completion. `G73` itself just executes the motion
below unconditionally until told otherwise.

Behavior:

1. First, retreat `R` mm downward from the current position — this is
   the page-separation "wiggle," and the **one and only** move this
   protocol permits to go downward while vacuum is engaged (the host is
   expected to have vacuum on and both blow relays in whatever state
   the process calls for at this point — the board has no visibility
   into that state, see the note in §10).
2. Then move monotonically upward to the absolute target `X`, unless
   interrupted by `M112` first.
3. Stop there, fully stationary, holding — no further motion, no
   automatic reversal. Responds `done G73 X<final_mm>` once reached. If
   `X` would meet or exceed the physical top-of-travel, the firmware
   substitutes a safe top-adjacent fallback position instead and reports
   that actual position, not the requested one — never allowed to reach
   the endstop (§3.6 applies here too).

Requires `M3` armed and a successful `G28` this session, same as
`G0`/`G1` — refused with `NOT_ARMED`/`NOT_HOMED` otherwise.

While `G73` executes, the host is expected to poll `?` (§5) to observe
the box crossing whatever positions its own process logic cares about
(e.g. engaging the turn-blower once past ~80% of page width) and issue
the corresponding commands to the Arduino (`monospace.md`) itself — this
board has no cross-board awareness and doesn't need any; it just reports
where it is, continuously, on request.

---

## 7. Touchdown and hold

**The scan photos are taken while the box is down, compressed against
the book at a configured press value, and completely stationary.** This
is the core safety-and-timing-critical sequence.

| Command | Meaning |
|---|---|
| `G30 [T<press_value>]` | **Touchdown.** Requires `M3` armed and a successful `G28` this session (`NOT_ARMED`/`NOT_HOMED` otherwise — the stop position it reports is only meaningful in the homed frame). Descend at the firmware's configured slow downward velocity with `motor.current_limit` set to the desired press value (`T`, in the calibrated force unit from §8; if omitted, use the persisted default press value). Detect contact via sustained velocity-sag-while-current-pinned (debounced — a single sample is not enough, startup acceleration also briefly dips velocity; dwell time is a persisted, tunable parameter). Once detected: **hold fully stationary** at that position and press value — no further motion of any kind — until the host sends `M24`. Responds `done G30 X<stop_position_mm> T<compression_reached>` once contact is detected and the hold begins (not once the hold ends). |
| `M24` | **Resume after hold.** Only valid while in a `G30` hold. Releases the hold; the next motion command the host sends is now permitted (whatever it is — this command doesn't itself move anything). If received while not holding: `error PICKUP_NOT_ARMED`. |

While holding after `G30`, **any** motion command other than `M24`/`M112`
must be refused, not queued and not silently accepted — the firmware
refuses to move until `M24` is received. This is firmware-enforced, not a
convention the host is trusted to follow.

**Implementation note, not a protocol detail:** whenever a move
immediately following a `G30` hold switches the internal SimpleFOC
`MotionControlType` to `angle`, the angle target must first be
initialized to the motor's *current* `shaft_angle` before adding travel —
otherwise the motor "unwinds" prior rotation instead of moving relative
to wherever this touchdown actually stopped (never the same position
twice). Rely on SimpleFOC's built-in clamped anti-windup for the
sustained current-saturation during the hold; don't reimplement it, and
don't reset PID state incorrectly between holds — this saturation
condition recurs on every single page.

---

## 8. Calibration commands

All three run entirely as firmware on this board, are **always
user-triggered, never automatic**, and physically move the motor.
Results are persisted on the ESP32 (flash/NVS) — never re-derived from
scratch on boot, never silently defaulted.

| Command | Meaning |
|---|---|
| `M40` | **Sensor/phase alignment** (`initFOC()`-equivalent). Runs SimpleFOC's alignment routine, which briefly moves the motor. Responds `done M40` or `error <reason>`. |
| `M41 R<revolutions>` | **Position calibration, step 1.** Rotates the motor shaft by the given number of revolutions from the current position at a safe calibration speed. The host/user then independently measures the resulting physical linear displacement (e.g. with calipers). Responds `done M41`. |
| `M41 X<measured_mm>` | **Position calibration, step 2.** Reports the externally measured displacement for the immediately preceding `M41 R<...>` call; firmware computes and persists the resulting mm-per-revolution constant. Responds `done M41 K<mm_per_rev>`. |
| `M42 A<amps>` | **Force calibration, sample step.** Holds the given current against an external test load the user has set up (e.g. box pressed on a scale, or a hung known weight). Responds `done M42` once the hold is stable. |
| `M42 T<measured_force>` | Reports the externally measured force for the immediately preceding `M42 A<...>` sample. Firmware accumulates `(current, force)` sample pairs; after enough samples (at least 3, spanning the intended operating range) computes and persists a current→force fit. Responds `done M42` (or `done M42 FIT` once a fit has been computed/updated). |

Exact curve-fit method (linear vs. piecewise) is not fixed here — needs
enough real samples to know which is appropriate; a simple linear fit is
an acceptable starting point.

---

## 9. Homing

Establishes the axis's absolute zero **at the top of travel** (the
homing reference is the upper endstop). Concretely: after a successful
`G28`, position `0` is at/very near the endstop, and every other
position in the travel range is reached by moving away from zero in
whichever direction the firmware's motor/encoder wiring defines as
positive (an implementation choice, fixed once and used consistently
everywhere — including by the host's own percentage-of-page-width
math). **Distinct from move-to-top (§10)** — different purpose and
trigger, even though both approach the same physical endstop.

| Command | Meaning |
|---|---|
| `G28` | **Home.** Requires `M3` armed first. Runs the seek+locate+pull-off sequence below. On success: sets the internal "homed" flag true, establishes position zero, responds `done G28 X0.0`. On failure (endstop never triggers within a sane travel bound): responds `error HOMING_FAILED`. |

**Sequence (two-phase seek + locate, standard CNC practice — a single
fast touch isn't precise enough to anchor every subsequent mm-based
move):**

1. **Seek (fast):** drive toward the endstop at higher speed until first
   trigger. Overshoot past the true trip point is expected at this
   speed.
2. **Back off:** retract a small, persisted, configurable pull-off
   distance until the switch releases, with a brief settling delay.
3. **Locate (slow):** re-approach at a much slower speed until it
   triggers again — this second touch is the recorded zero reference,
   not the first.

Both touches within one `G28` cycle are legitimate, expected endstop
contacts (§3.6 doesn't apply to them).

**The "homed" flag is cleared on every boot** and only set by a
successful `G28` in the current session — position/homed state never
survives a power cycle. Every absolute-position command (`G0`, `G1`,
`G73`, `G30`) is refused with `NOT_HOMED` until `G28` succeeds.

`G28` itself must only be sent by the host after the host has obtained
explicit user confirmation that it's acceptable for the box to move —
that gate is the host's (sans-serif's) responsibility, not something
this firmware enforces beyond simply requiring `M3` first.

---

## 10. Move-to-top (recovery move)

**No dedicated command.** "Move to top" — used both for the touchscreen
recovery button and for parking the box at the end of a job before
`M5` — is a **plain `G0`** to a small absolute value near position `0`
(the homing reference, established at the top of travel, §9), on the
side away from the endstop. There is no separate margin value the
firmware persists for this — the host supplies the target `X` directly,
same as it does for every other `G0`/`G1` call.

- Requires homed (`NOT_HOMED` if `G28` hasn't succeeded this session) —
  the same generic gating every absolute move already has, no special
  case needed.
- Available at any time the driver is armed, including immediately
  after an `M112` stop or a mid-sequence abort.
- If this move ever does trigger the endstop, that's not a successful
  arrival — it falls under §3.6's hard-fault rule, same as any other
  move.

**Recommended, not yet decided:** the firmware could defensively refuse
a downward move other than `G73`'s embedded retreat while it has been
told (via an optional notification command, not yet specified) that
vacuum is currently engaged on the Arduino side, as a second line of
defense beyond the host happening to sequence commands correctly. This
would require the host to forward vacuum-engagement state to this board
(the ESP32 has no direct visibility into the Arduino relay state
otherwise). Flagged here as a future extension, not required for the
MVP protocol above.

---

## 11. Drive mode — position-based vs. torque-based (proposal, not yet confirmed)

Requested but not yet decided with ijon/hrmny — presented here as a
concrete proposal for confirmation, not as settled behavior.

| Command | Meaning |
|---|---|
| `M50` | **Position-based mode** (default after `G28`). Subsequent `G0`/`G1`/`G73` moves use SimpleFOC `angle` control: the firmware drives hard to the exact commanded target regardless of resistance, up to the safety `current_limit` ceiling only (§3.7) — that ceiling is a safety cap here, not a target press value. |
| `M51 T<current_or_force_limit>` | **Torque-based mode.** Subsequent `G0`/`G1`/`G73` moves use the same underlying mechanism as touchdown (§7): `velocity` control with `current_limit` pinned at `T`. The move is **compliant** — if resistance pins the current at the limit before the target is reached, the firmware stops there rather than forcing through, and reports the position actually reached, not necessarily the requested target. |

Both are stateful/modal (persist until the other is sent), same pattern
as `M3`/`M5` elsewhere in this protocol — chosen specifically so no
parameter letter has to double as a mode selector (`S` is off-limits,
§4.4).

**Rationale for this split:** most of the flip-cycle process needs
precise, unyielding positioning (position-based) because pickup/turn
detection already happens via separate pressure telemetry, not via the
motor stalling. Torque-based mode generalizes touchdown's
already-proven compliant-stop mechanism to any move, for future cases
where "stop safely on resistance" is the desired behavior outside of
touchdown specifically.

**Alternative considered, not recommended:** a single `M50` with a mode
parameter (`M50 <mode-letter><value>`) instead of two distinct codes.
Rejected because there's no parameter letter left to spend on it without
either colliding with a real-G-code meaning (`S`) or overloading `T`
(which already means "torque/force value," not "which mode") — two
plain codes, the same pattern `M3`/`M5` already use for job state,
avoids the problem entirely.

**Both `done G0 ...`/`done G1 ...` responses in torque-based mode must
report the position actually reached**, which may differ from the
commanded target — this is a real behavioral difference from
position-based mode's `done`, not just a cosmetic one, and the host must
be able to tell the two situations apart (the host already knows which
mode is active, since it's the one that set it).

---

## 12. Telemetry

Reported continuously via `?` (§5) at any time: current position (mm,
`?` if not yet homed), current motor current/torque (Amps, or the
calibrated force unit once §8's fit exists), homed flag, and coarse
state (`Idle`/`Armed`/`Moving`/`Holding`/`Homing`/`Fault`).

`G30`'s `done` response additionally reports the stop position and
compression/press value reached at the moment contact was detected —
this pairing (position + press value at touchdown) is what the host
correlates with a separately-measured air-pressure reading for
diagnostics; it must be accurate to the actual detected touchdown event,
not a nominal/requested value.

---

## 13. Units

The host (`sans-serif.md`) only ever sends and receives **mm** for
position and the **calibrated force unit from §8** for press
values/current limits where applicable — never raw radians or a
non-calibrated Amps value in normal (post-calibration) operation. The
board owns all angle↔mm and current↔force conversions internally,
using the constants persisted by §8's calibration commands. The host
keeps no separate copy of these constants to reconvert with.

---

## 14. Worked example: one full job

Illustrative only — exact values are placeholders, and the pickup-check/
Arduino-coordination detail belongs to `sans-serif.md` §5, not this
board. Shows the shape of a session, including both the "another
page-turn" and "job done" branches:

```
> M3                          arm the driver
< ok
< done M3

> G28                         home (only after UI confirm)
< ok
< done G28 X0.0

  -- per page slot --
> G30                         touchdown (uses persisted default press)
< ok
< done G30 X142.3 T2.1

  (capture_pair() happens on the host, outside this protocol)

> M24                         resume — box may move again
< ok
< done M24

> G73 X10.5 R4.0               retreat 4mm, then climb to the 105-120%
< ok                          point — sent right away, no separate
                               "check" command exists
  (host reads differential pressure on its own partway through this
   move, polling `?` concurrently — two possible outcomes:)

  -- outcome A: pickup confirmed, do nothing --
  (host keeps polling `?` to time the turn-blower relay on the
   Arduino; lets G73 run to completion)
< done G73 X10.5

  -- next page slot starts right here: --
> G30                         descends from X10.5 until contact — this IS
< ok                          the "next downward motion towards the book"
< done G30 X138.9 T2.0

  -- outcome B: pickup failed, host cancels the move --
> M112                        (sent instead of waiting for G73 to finish)
< ok
< done M112                   (G73 itself never gets its own response —
                               see §5)
  (host also switches off the separation fan and vacuum via monospace.md)
> G30                         fresh touchdown for the retry, wherever
< ok                          M112 left the box
< done G30 X140.1 T2.1

  ... repeat ...

  -- job finished --
> G0 X2.0                     move to top: small X near the homed zero
< ok
< done G0 X2.0

> M5                          end job, disarm
< ok
< done M5
```

---

## Reference

Exhaustive hardware tables (motor/board datasheet values, full GPIO map,
connector pinouts, PlatformIO toolchain settings) and the still-open
hardware-verification items not resolvable without the physical board in
hand: [`reference/esp32-foc-firmware-requirements.md`](reference/esp32-foc-firmware-requirements.md).
