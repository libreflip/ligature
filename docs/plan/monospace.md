# Monospace — Arduino Firmware Specification

> Target-state spec for the Arduino firmware controlling the machine's
> relays. Self-contained: written to be handed to a firmware implementer
> with no other file access.
>
> This board is **dumb**, same principle as the ESP32 FOC board
> (`bldc-driver.md`): it never decides *when* to switch anything on. It
> receives one command per line from the Raspberry Pi over USB-serial,
> executes it, and acknowledges. All process logic (when to engage
> vacuum, when a page has separated, when to abort) lives on the
> Raspberry Pi (`sans-serif.md`).

---

## 1. Scope

Owns exactly these physical actuators:

- **Vacuum pump** relay
- **Page-separation fan** relay
- **Turn blower** (positive-pressure pump) relay
- **Light** relay

**Explicitly not this board's concern:**
- Suction-box motor/lift control — that's the ESP32 FOC board
  (`bldc-driver.md`).
- The upper endstop — physically wired to the ESP32 FOC board, not this
  board.
- Air-pressure sensing (BMP180) — wired directly to the Raspberry Pi via
  I2C, not to this board.
- A pneumatic pressure sensor — **not currently in scope**, see §7.

---

## 2. Electrical facts (must hold regardless of protocol design)

- All four relay outputs (vacuum, fan, blower, light) are **active-low**:
  writing the pin LOW energizes the relay, HIGH de-energizes it.
- On boot, before anything else, drive all four relay pins **HIGH**
  (= off) — no actuator may be left in an undefined or energized state
  at power-up.
- Exact GPIO pin assignments for each relay: see
  `docs/hardware/electronics.md` (as-built wiring reference, not
  duplicated here since it's a hardware fact, not a software decision).

---

## 3. Safety requirements

1. **No actuator may be energized at boot** — all relays init to their
   off (HIGH) state before the serial interface starts accepting
   commands.
2. **No automatic behavior of any kind.** This firmware never times an
   actuator, never sequences multiple actuators on its own, and never
   turns anything on except in direct, immediate response to a host
   command on the current line. All sequencing (how long vacuum stays
   on, when the fan comes on relative to the blower) is the Raspberry
   Pi's job.
3. **`ALL OFF` (§5) must be atomic** — vacuum, fan, and blower all end up
   off as a result of one command, not three commands that could
   partially fail if the host loses the connection mid-sequence.

---

## 4. Command/response protocol

Text-based, one command per line, newline-terminated, ASCII. Every
command gets exactly one response line: `OK` on success, or
`ERR <reason>` on failure (unrecognized command, malformed line). No
unsolicited messages.

---

## 5. Commands

| Command | Response | Meaning |
|---|---|---|
| `VACUUM ON` | `OK` | Energize the vacuum pump relay |
| `VACUUM OFF` | `OK` | De-energize the vacuum pump relay |
| `FAN ON` | `OK` | Energize the page-separation fan relay |
| `FAN OFF` | `OK` | De-energize the page-separation fan relay |
| `BLOWER ON` | `OK` | Energize the turn-blower relay |
| `BLOWER OFF` | `OK` | De-energize the turn-blower relay |
| `LIGHT ON` | `OK` | Energize the light relay (forced on) |
| `LIGHT OFF` | `OK` | De-energize the light relay (forced off) |
| `LIGHT AUTO` | `OK` | Light relay is driven by this firmware's own auto logic (ambient-based or otherwise) rather than the host's explicit on/off state. Exact auto-trigger condition is a firmware implementation detail, not fixed here — the host only needs to know that after `LIGHT AUTO`, it no longer controls the light directly, until it sends `LIGHT ON`/`LIGHT OFF` again. |
| `ALL OFF` | `OK` | Atomically de-energizes vacuum, fan, and blower in a single operation (light is untouched). Used by the Raspberry Pi's Stop-button handling. |

Any other line: `ERR UNKNOWN_COMMAND`.

---

## 6. Framing

The lower-level byte-stream framing (how a line is delimited and parsed
on both the Arduino and the Rust client) can reuse the generic
length-prefixed/line-based serial read/write loop pattern already
proven in `sans-core::hardware`'s serial-port plumbing — that plumbing
is transport-level and independent of the specific command vocabulary
above, so it's a valid starting point for the Rust client side even
though the command set itself is new.

---

## 7. Not currently in scope: pneumatic pressure sensor

A pneumatic pressure sensor on this board is **not decided** and not
part of the protocol above. It would only become relevant if a
currently-undecided hardware rework happens — replacing the
page-separation fan and turn blower with compressed air and pneumatic
valves. If and when that rework is decided, this board's scope and
command set would need revisiting (a sensor-read command, and likely
`FAN`/`BLOWER` being replaced by valve-control commands rather than
staying simple relay on/off) — not scoped further here since the
rework itself isn't confirmed.
