# Monospace — Arduino Firmware Specification

> Target-state spec for the Arduino firmware controlling the machine's
> relays. Self-contained: written to be handed to a firmware implementer
> with no other file access.
>
> This board is **dumb**, same principle as the ESP32 FOC board
> (`ligature.md`): it never decides *when* to switch anything on. It
> receives one command per line from the Raspberry Pi over USB-serial,
> executes it, and acknowledges. All process logic (when to engage
> vacuum, when a page has separated, when to abort) lives on the
> Raspberry Pi (`sans-serif.md`).
>
> **Code reuse intent (ijon, 2026-08-02):** the existing `monospace`
> Arduino firmware being extended here (§8 step 5, base per
> `architecture.md` AD-001) was originally written by segfault. Wherever
> it can sensibly carry over into this spec's protocol/behavior — relay
> handling, the BMP180 integration, general structure — prefer adapting
> that existing code over rewriting the equivalent behavior from
> scratch. This is a standing preference for whoever implements this
> spec, not just a one-off starting point.

---

## 1. Scope

Owns exactly these physical actuators and sensors:

- **Vacuum pump** relay
- **Page-separation fan** relay
- **Turn blower** (positive-pressure pump) relay
- **Light** relay
- **Start/Stop/E-Stop button + RGB status LED** (added to scope
  2026-08-08, **implemented and merged, `monospace` PR #5 — see §10 for
  the full spec**) — a single momentary pushbutton with an integrated
  tri-colour LED ring, physically wired and confirmed on the real board.
  Same "dumb board" principle as everything else in
  this document: this board debounces and reports button presses, and
  drives the LED to whatever color/pattern the Raspberry Pi last
  commanded — it never decides on its own what the LED should show or
  what a button press means.
- **BMP180 air-pressure sensor** (I2C) — physically wired here (Arduino
  pins A4/SDA, A5/SCL, wired 2026-07-25) and stays here: the Arduino has
  little else to do, and moving the sensor to the Raspberry Pi would mean
  rewiring something that already works for no functional gain. See §6
  for the read protocol. (This reverses an earlier planning decision,
  `architecture.md` AD-001, that had proposed moving it to the Raspberry
  Pi — corrected 2026-08-01.)

**Explicitly not this board's concern:**
- Suction-box motor/lift control — that's the ESP32 FOC board
  (`ligature.md`).
- The upper endstop — physically wired to the ESP32 FOC board, not this
  board.

---

## 2. Electrical facts (must hold regardless of protocol design)

- All four relay outputs (vacuum, fan, blower, light) are **active-low**:
  writing the pin LOW energizes the relay, HIGH de-energizes it.
- On boot, before anything else, drive all four relay pins **HIGH**
  (= off) — no actuator may be left in an undefined or energized state
  at power-up.
- **Arduino Uno digital pin assignments** (inherited unchanged from the
  existing `monospace` firmware's `bookscanner.cpp`, `#define`s at the
  top of the file — reuse these constants directly rather than
  reassigning pins):
  | Pin | Function |
  |---|---|
  | D2 | Start/Stop/E-Stop button contact (`INPUT_PULLUP`, active-low: pressed = LOW) — see §10 |
  | D4 | Vacuum pump relay (`VAC_PUMP`) |
  | D5 | Turn blower relay (`PES_PUMP` in the old code — "positive pressure pump", same thing) |
  | D6 | Light relay (`LAMP`) |
  | D7 | Page-separation fan relay (`FAN`, called "flutter fan" in older docs/code) |
  | D9 | RGB status LED, Red channel (PWM) — see §10 |
  | D10 | RGB status LED, Green channel (PWM) — see §10 |
  | D11 | RGB status LED, Blue channel (PWM) — see §10 |
  | A4 | BMP180 SDA (hardware I2C, not reassignable) |
  | A5 | BMP180 SCL (hardware I2C, not reassignable) |
  **D2/D9/D10/D11: wired, implemented, and verified on the real board
  (ijon, 2026-08-08, `monospace` PR #5)** — button contact on D2, RGB
  cathodes on D9–D11, LED common anode on 5V, polarity confirmed by a
  diode-test (§10.3).
  **Not this board's concern anymore, even though the old code still
  defines them** — don't carry these over: `A0` (`LIM_SW`, the old
  endstop switch — moved to the ESP32 FOC board, see §1), and `D2`/`D3`/
  `D8`/`D9` (`STALLPIN`/`FAULTPIN`/`SLEEPPIN`/`CSPIN`, the old stepper
  driver — motor control moved to the ESP32 entirely, this firmware has
  no motor code at all).
  **Verified (ijon, 2026-08-02):** this table was flagged as
  not-yet-individually-reconfirmed after the 2026 rebuild; it has since
  been checked against the real board and confirmed correct, no
  surprises — see `docs/hardware/electronics.md` §2.9 and
  `architecture.md` AD-007.

---

## 3. Safety requirements

1. **No actuator may be energized at boot** — all relays init to their
   off (HIGH) state before the serial interface starts accepting
   commands. **Known exception, accepted for now (ijon, 2026-08-02):**
   on the current hardware, the light is observed to switch on when the
   Arduino boots. This is fine and does not need to be fixed as part of
   this spec — the light isn't a safety-critical actuator the way
   vacuum/fan/blower are. Revisit this only if/when `LIGHT AUTO` (§5,
   currently excluded from MVP) actually gets implemented in the
   future.
2. **No automatic behavior of any kind, with two explicit exceptions the
   host opts into on purpose:** `LIGHT AUTO` (§5, **excluded from MVP**,
   see there) and `PRESS START` (§5/§6). Outside of those two opt-in
   modes, this firmware never times an actuator, never sequences
   multiple actuators on its own, and never turns anything on except in
   direct, immediate response to a host command on the current line.
   All sequencing (how long vacuum stays on, when the fan comes on
   relative to the blower) is the Raspberry Pi's job.
3. **`ALL OFF` (§5) must be atomic** — vacuum, fan, and blower all end up
   off as a result of one command, not three commands that could
   partially fail if the host loses the connection mid-sequence.
4. **Enable the AVR watchdog timer** (e.g. an 8s timeout, the longest
   the hardware watchdog supports without a software-reset chain) so
   that a firmware hang (the most likely cause: an I2C bus lockup while
   reading the BMP180, a known failure mode for that sensor family)
   forces a reset rather than leaving relays frozen in whatever state
   they were in when the hang occurred. This doesn't add new automatic
   behavior — a watchdog reset re-runs the normal boot sequence, which
   already forces everything off (point 1 above and §2). Compare the
   ESP32 FOC board's equivalent rule (`ligature.md`): a reset of any
   kind, including a watchdog reset, never resumes or remembers
   whatever was happening before it. **Implemented as specified
   (`monospace` PR #2, merged):** `wdt_enable(WDTO_8S)`, armed in
   `setup()` before the BMP180 driver's `begin()` call — exactly
   because that driver's I2C read helper has an unbounded wait with no
   timeout of its own, the specific failure mode this guards against.
5. **Known, harmless boot-time quirk, not a bug to chase:** the Uno's
   USB-serial bootloader briefly resets the chip on every upload and on
   every USB-DTR reconnect. Digital pins are in an undefined/floating
   state for a short window before `setup()` runs and forces them HIGH
   — in practice this can produce a brief, faint relay click on every
   flash or reconnect. Expected Arduino Uno behavior, not something to
   try to eliminate.
6. **Decided (ijon, 2026-08-02): no firmware-side host-timeout.** If
   the host disappears while the firmware itself keeps running fine
   (RPi crash, USB cable pulled, etc.), an actuator left on by the last
   command received stays on indefinitely — the firmware does not time
   out and force `ALL OFF` on its own. This firmware stays strictly
   "dumb" per this spec's opening framing: it reacts only to explicit
   commands, never to elapsed time. Monitoring for a disappeared host
   (and deciding what to do about it) is the Raspberry Pi application's
   responsibility (`sans-serif.md`), not this board's — don't add
   host-timeout behavior here.
7. **RGB status LED boots off, no exception:** all three LED PWM
   outputs (D9/D10/D11) init to their off level before the serial
   interface starts accepting commands, exactly like the four relays
   (point 1) — the LED stays off until the Raspberry Pi sends its first
   `LED SET` (§10). Unlike the light relay, there is no accepted
   boot-quirk exception here. **Implemented as specified (`monospace`
   PR #5, merged):** `set_led(0,0,0)` in `begin()`, before the serial
   interface starts accepting commands.
8. **Button debounce is signal conditioning, not the "no automatic
   behavior" rule's concern:** point 2 above prohibits this firmware
   from *deciding when to act* on its own. Debouncing the button's raw,
   physically noisy contact signal before reporting one clean `EVENT
   BUTTON PRESSED` line (§10) is the same category of thing as the
   BMP180 multi-sample averaging (§6) — cleaning up a noisy physical
   signal, not a process decision. It doesn't fall under point 2 and
   isn't a third opt-in automatic mode alongside `LIGHT AUTO`/`PRESS
   START`. **Implemented as specified (`monospace` PR #5, merged):**
   time-based debounce, `BUTTON_DEBOUNCE_MS = 30`.

---

## 4. Command/response protocol

Text-based, one command per line, newline-terminated, ASCII. Every
command gets exactly one response line: `OK` on success, or
`ERR <reason>` on failure (unrecognized command, malformed line). No
unsolicited messages — **with two exceptions:** while continuous
pressure streaming is active (`PRESS START`, §6), the board also emits
unsolicited `PRESS <mbar>` lines interleaved with normal command
responses; and, **added 2026-08-08, implemented and merged (§10,
`monospace` PR #5)**, the board emits an unsolicited `EVENT BUTTON
PRESSED` line every time the status-LED button is debounced-pressed,
independent of whatever streaming state is active. The host tells all
three kinds of line apart by prefix — `PRESS ` or `EVENT ` — every line
that starts with neither remains a direct response to whichever command
immediately preceded it, unaffected by either kind of unsolicited
traffic.

**Concrete framing rules (not left to implementer discretion, both
sides must agree on these exactly):**
- **Baud rate: `115200`.** Fixed as of the merged implementation
  (`monospace` PR #2, `master`) — deliberately not the old firmware's
  `9600` or the old Rust client's `9200`; that exact mismatch between
  the two legacy sides was a real, previously undetected bug (see the
  `sans`/`monospace` code review), so this was picked and confirmed
  working on both sides rather than left as an open implementer choice.
- **Line ending:** `\n` (LF) terminates a line on both directions of
  the link. Accept an optional preceding `\r` when parsing incoming
  lines (i.e. tolerate `\r\n`) — cheap to support, avoids fights with
  whatever line-ending convention a given serial library/terminal
  defaults to; don't require callers to get this exactly right.
- **Case:** commands are case-sensitive, uppercase only, exactly as
  written in §5's table. `vacuum on` is `ERR UNKNOWN_COMMAND`, not a
  tolerated variant.
- **Buffer size:** the Arduino Uno's hardware serial RX buffer is 64
  bytes by default. Every command in §5 is well under that, so this
  isn't a practical problem for the current command set — but keep it
  in mind if a future command needs a longer parameter (e.g. anything
  resembling free text) and don't assume an arbitrarily long line is
  safe to send.
- **No state-query command, by design.** There's no way to ask "is the
  vacuum currently on?" — the host is expected to track the state it
  last commanded itself. Practical consequence for whoever writes the
  RPi-side client (including `hw_diag`, §9): send `ALL OFF` once,
  immediately after opening the serial connection, before doing
  anything else — that's the recovery pattern for "reconnecting to a
  board that might already be in some state from a previous session,"
  not a query.

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
| `LIGHT AUTO` | `OK` | **Excluded from MVP (ijon, 2026-08-02) — not needed for now, maybe later.** Kept documented here as a possible future addition, not to be implemented as part of the MVP build. If/when it is picked up: light relay driven by this firmware's own auto logic (ambient-based or otherwise) rather than the host's explicit on/off state. Exact auto-trigger condition would be a firmware implementation detail, not fixed here — the host would only need to know that after `LIGHT AUTO`, it no longer controls the light directly, until it sends `LIGHT ON`/`LIGHT OFF` again. |
| `ALL OFF` | `OK` | Atomically de-energizes vacuum, fan, and blower in a single operation (light is untouched). Used by the Raspberry Pi's Stop-button handling. |
| `PRESS?` | `OK <mbar>` | Single-shot pressure read: takes several BMP180 samples, averages them, returns one value. Used wherever only one reading is needed and accuracy matters more than latency (e.g. an ambient baseline). |
| `PRESS START` | `OK` | Begin continuous pressure streaming (§6): after this `OK`, the board also emits unsolicited `PRESS <mbar>` lines at as fast a rate as achievable, until `PRESS STOP`. |
| `PRESS STOP` | `OK` | Stop continuous pressure streaming. |
| `LED SET <r> <g> <b>` | `OK` | **Added 2026-08-08, implemented and merged — see §10.** Sets the status-LED's three channels to the given brightness values, `0`–`255` each, decimal, space-separated. Takes effect immediately, stays until the next `LED SET`. This is the *only* LED command — blinking is the host sending repeated `LED SET` calls at whatever cadence it wants (§10), not a firmware mode. Malformed args (wrong count, non-numeric, out of range) return `ERR BAD_ARGS`. |

Any other line: `ERR UNKNOWN_COMMAND`.

**Not a command — unsolicited only, added 2026-08-08, implemented and
merged (§10):** `EVENT BUTTON PRESSED`, emitted by the board on its own initiative whenever the
status-LED button is debounced-pressed. Never sent in response to a
host command, and has no `OK`/`ERR` response of its own — see §4's
framing rules for how the host tells this apart from a command
response.

---

## 6. Pressure sensing (BMP180)

Two read modes, chosen by the Raspberry Pi depending on what it needs at
that moment (`sans-serif.md` §1.2):

- **Single-shot (`PRESS?`):** used wherever only one reading is needed
  and accuracy matters more than latency — e.g. the once-per-attempt
  ambient baseline read (`sans-serif.md` §4 step 5, §5.2 step 1). Takes
  a larger number of BMP180 samples, averages them, returns one value.
- **Continuous streaming (`PRESS START`/`PRESS STOP`):** used only while
  the pickup-success check needs a steady stream of readings rather than
  one point-in-time value — during `G73` (`sans-serif.md` §5.2 step 9)
  and during the calibration ascent (`sans-serif.md` §4 steps 7–9).
  Averages a smaller number of samples per emitted value, favoring rate
  over per-reading accuracy. **Target: at least 5 readings/second
  reaching the Raspberry Pi, ideally up to 50.** **Confirmed achievable
  (2026-08-01/02):** ~49 readings/second measured on real hardware at
  BMP180 oversampling=2, comfortably clearing the target and close to
  the ideal, with noticeably less per-sample sensor noise than the
  faster-but-noisier oversampling=0 that was also tried. Relay commands
  sent during an active stream continued to work correctly, confirmed
  during a real multi-attempt pickup session — see
  `docs/hardware/bmp180-vacuum-drop-test.md`.

**`<mbar>` number format (fixed here, not an implementation choice):**
plain decimal, fixed at 2 decimal places, no unit suffix, no thousands
separator — e.g. `1013.25`, not `1013.2456`, `1013`, `1013.25mbar`, or
`101325`. Both `OK <mbar>` (`PRESS?`) and `PRESS <mbar>` (streaming)
use this same format. Pin this down exactly since two independently
buildable things (firmware, RPi client) need to parse it identically.

Sample counts per averaged reading (single-shot vs. continuous):
continuous streaming uses BMP180 oversampling=2 (chosen over
oversampling=0 for its lower per-sample noise at ~49 Hz, see above);
the exact single-shot (`PRESS?`) sample count wasn't separately
reported and can be treated as an implementation detail of the merged
firmware (`monospace` PR #2) rather than something this spec needs to
pin down further.

Note: this section covers only the BMP180 (inside the suction box,
I2C, wired 2026-07-25) — this is unrelated to the separate analog
pneumatic-safety-sensor idea that was previously floated for a possible
future rework (compressed-air nozzles + solenoid valves replacing the
fan/blower). That idea is dropped for now, not part of this board's
scope, and not referenced anywhere in this document — see
`architecture.md` AD-001/AD-006 if it needs to be reconsidered later.

---

## 7. Framing

The lower-level byte-stream framing (how a line is delimited and parsed
on both the Arduino and the Rust client) can reuse the generic
length-prefixed/line-based serial read/write loop pattern already
proven in `sans-core::hardware`'s serial-port plumbing — that plumbing
is transport-level and independent of the specific command vocabulary
above, so it's a valid starting point for the Rust client side even
though the command set itself is new.

**One addition needed on top of that base pattern:** while `PRESS
START` streaming is active (§6), incoming lines are no longer strictly
one-response-per-command — the Rust client's read loop must classify
every line it receives by its prefix (`PRESS ` = telemetry, anything
else = the response to the next outstanding command) rather than
assuming a strict request/response pairing for the duration of the
stream.

---

## 8. Toolchain: where to build and flash this firmware

**Build and flash directly from the Raspberry Pi that the Arduino is
already connected to — not from a separate development laptop.**
(ijon/Claude, 2026-08-01, research + recommendation. **Acted on
2026-08-01/02** — the recommendation below was followed for the actual
`feature/text-protocol`/`feature/hw-diag` implementation work, both now
merged to `master`.)

Reasoning:
- The Raspberry Pi is the permanent USB-serial host for this board in
  the target architecture (`sans-serif.md` §1.2) — flashing over the
  same physical connection that production traffic will later use
  avoids any risk of the dev path (cable/port/driver) behaving
  differently from the real one.
- The Arduino is physically wired into the machine already (relay
  board, BMP180 on A4/A5) — moving it to a laptop for each flash cycle
  means repeated unplugging of live wiring, not just a USB cable.
- Claude Code is already installed and working on this Raspberry Pi
  (used successfully for the touchscreen-rotation task, 2026-07-25) —
  the same session that writes/edits this firmware can build, flash,
  and then immediately open the serial connection to test the actual
  protocol above against the real hardware, without switching machines
  in between.
- `arduino-cli` runs natively on Raspberry Pi 4 (ARM64 or ARMv7,
  depending on the installed OS) — official install script
  auto-detects the platform, no GUI/IDE needed, fits the existing
  headless/CLI workflow.

Practical steps for whoever (or whichever Claude Code session) does
this on the Raspberry Pi:
1. `curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh`
   — needs internet once, works offline afterward.
2. `arduino-cli core install arduino:avr`.
3. Add the `pi` user to the `dialout` group so `/dev/ttyACM0`/
   `/dev/ttyUSB0` is usable without `sudo`.
4. `arduino-cli board list` to identify the correct port — **the ESP32
   FOC board (`ligature.md`) is a second, independent USB-serial
   connection to the same Raspberry Pi**, so don't assume there's only
   one port to pick from.
5. Get the existing `monospace` firmware source onto the Raspberry Pi
   (base per `architecture.md` AD-001 — extended, not rewritten from
   scratch) and build against it. The `SFE_BMP180` driver (§1, reuse
   as-is) is vendored directly inside the sketch folder next to the
   main `.ino`, not a separately installed Arduino library — no
   `arduino-cli lib install` step needed for it, it compiles in
   automatically as part of the sketch.
6. After flashing, `arduino-cli monitor -p <port> -c baudrate=115200`
   (baud rate as fixed in the merged firmware — deliberately not the old
   firmware's `9600` or old Rust client's `9200`, see §4) to manually
   exercise the commands in §5 against the real board before wiring up
   the Rust client (§9).

---

## 9. RPi-side diagnostic CLI (`hw_diag`)

> **Status: built and merged (2026-08-01/02)** — `sans` PR #6 → `master`
> (`beab768`). The design below is kept as-is (not rewritten in past
> tense) since it remains the accurate spec for this tool; where the
> real implementation confirmed or settled something the spec below
> left open, that's called out inline.
>
> **Extended again for §10 (2026-08-08, `sans` PR #7, `4d8cdfa`):** now
> also covers `LED SET` (interactive `led <r> <g> <b>` command) and
> prints incoming `EVENT BUTTON PRESSED` lines — see §10.4 for detail.

Not firmware — a companion tool on the Raspberry Pi, needed to validate
this board (especially the BMP180) on real hardware and manually
exercise the relays, ahead of full integration into the eventual
`sans-serif.md` application. Building it is part of this same pass, not
optional/future work.

### 9.1 Operating model — read this before designing the CLI surface

**Critical constraint: the Arduino Uno resets on every new serial
connection** (its standard DTR-triggered auto-reset circuit — the same
mechanism `arduino-cli upload` itself relies on). Opening the port
forces a reboot: all relays briefly drop to the safe boot-off state,
then there's a short boot delay (measure the exact time on real
hardware) before the firmware responds to anything again.

**Consequence for this tool's shape:** `hw_diag` must NOT be a classic
"one process invocation per command" CLI (e.g. `hw_diag vacuum on`,
then separately `hw_diag press-stream`) — each invocation would reopen
the port and reset the board, which would, for example, turn the
vacuum back off the moment you started watching the pressure stream in
a second invocation. It also isn't possible to run two `hw_diag`
processes against the same port at once — only one process can hold a
serial port open.

**Instead: `hw_diag --port /dev/ttyACM0` opens the connection once and
stays running as a single interactive session** for as long as you
need it — no scripted one-shot mode, that's explicitly not needed here
(ijon, 2026-08-02). A session looks like this:

1. Start it: `./target/release/hw_diag --port /dev/ttyACM0`.
2. The tool opens the port once, waits out the post-reset boot delay,
   then automatically sends `ALL OFF` (the reconnect pattern from §4)
   before showing a prompt — so every session starts from a known
   state without you having to remember to do that yourself.
3. From then on it's a live, transparent terminal: **you type a
   command from §5's table, the tool sends it, and prints back exactly
   what the Arduino replied** (`OK`, `OK <mbar>`, `ERR <reason>`) — no
   hiding or silently absorbing the response into some abstracted
   "success" indicator. Seeing the real wire response is the point of
   a diagnostic tool. Accept commands case-insensitively at the prompt
   for typing convenience (auto-uppercase before sending) — that's a
   local input-convenience choice, not a change to the wire protocol's
   own case-sensitivity rule (§4).
4. `PRESS START` behaves exactly as §4/§6 describe: the connection
   enters streaming mode, and every unsolicited `PRESS <mbar>` line is
   printed **as it arrives**, timestamped, interleaved with whatever
   else you type in the meantime — you can still send relay commands
   while a stream is running, same as the real protocol allows. If
   `--log <file.csv>` was given at startup, every streamed line is
   *also* appended to that file, not instead of printing it. `PRESS
   STOP` ends the stream.
5. Exit the session (e.g. Ctrl-D) to close the connection.

This is deliberately close to what `arduino-cli monitor` already gives
you (§8 step 6) — the same one-connection-stays-open model — plus
timestamped output and optional CSV logging, which is what actually
matters for deriving pickup-success/-failure thresholds from the data
afterward; plain terminal scrollback alone isn't enough for that.

### 9.2 Implementation notes

- **Where:** new binary `sans-core/src/bin/hw_diag.rs` in the `sans`
  Rust workspace (`git@github.com:libreflip/sans.git`), following the
  existing `camcal.rs` precedent already in that crate (a small
  standalone diagnostic tool living next to the main library, not a
  new top-level crate).
- **Build the underlying protocol client as a proper typed Rust API**
  (e.g. `fn press_once() -> Result<f32>`, `fn set_vacuum(on: bool) ->
  Result<()>`, matching `sans-serif.md` §1.2's function names) —
  `hw_diag`'s interactive terminal is a thin UI on top of that API, not
  a special-case implementation of its own. This typed client is the
  real, permanent deliverable T21/T22 need; the terminal is just how
  you exercise it by hand right now.
- **`--port` is a required argument, never auto-detected.** Two
  independent USB-serial devices are typically attached to the same
  Pi (this Arduino and the separate ESP32 FOC board) — guessing which
  is which would risk sending a relay command to a motor-control board
  or vice versa. Make the caller specify the port explicitly every
  time rather than picking "the first serial device found."
- **Suggested CLI arg parsing (just for the startup flags, `--port`/
  `--log`):** `clap`, for consistency with the existing (currently
  unimplemented) `sans-ctrl` — not a requirement, just avoids a second
  CLI-parsing convention in the same workspace for no reason.
- **Zero-code fallback while this binary doesn't exist yet:**
  `arduino-cli monitor` (§8 step 6) already gives you the same
  one-connection interactive session, no logging though — use it for
  the first "does the board even respond" check the moment firmware is
  flashed, in parallel with building `hw_diag`.
- **Verification order:** confirm each command via `arduino-cli
  monitor` first, *then* re-verify the same command through `hw_diag`
  once it exists — the CLI's own serial handling is new code and needs
  its own check, not just a port of already-verified firmware
  behavior.
- **What not to reuse for this:** `sans-core::hardware`'s existing
  `protocol.rs` (`Command`/`Response` encoding) is hard-wired to the
  *old* binary 3-command protocol (`BOX`/`LIGHT`/`FLIP`) and doesn't
  fit this new text protocol — only the general shape of
  `hardware/mod.rs` (open the port via the `serialport` crate, read on
  a dedicated thread) is a reasonable structural reference. `sans-ctrl`
  is a different, unrelated tool (a future `sans-server` daemon's
  remote-control CLI, not direct serial access) — don't extend it for
  this, and don't build a daemon for this tool either — the interactive
  single-session model above already solves the problem a daemon would
  otherwise be there to solve.
- **Fallback only, not the default path:** if the Rust path stalls,
  `monospace/arduinofucker/arduinofucker.py` (an old interactive Python
  test tool in this same repo, itself already built around one
  open-once connection with a background reader thread — the same
  operating model as §9.1) could be adapted to the new protocol instead
  — say clearly if this ends up being needed.
- **Out of scope for this tool:** wiring it into an Auto-Scan loop, a
  Stop-button handler, or any UI; turning derived thresholds into
  actual pickup-success/-failure decision logic. This tool exists to
  produce the measurement capability and the data, nothing downstream
  of that.
- **Heads-up:** `sans-core/Cargo.toml` pins `serialport = "3.2"` (from
  2018) — may need a version bump against the current Rust toolchain on
  today's Raspberry Pi OS; that's updating an already-approved
  dependency, not adding a new one.
- **Recommended, not mandated:** given this is a full wire-protocol
  rewrite on this board's side plus a new binary on the `sans` side,
  prefer a feature branch in each repo and a PR against `master` once
  both work end-to-end against real hardware, rather than landing an
  in-progress rewrite directly on `master`. Direct-to-`master` is fine
  too if ijon says so explicitly.

---

## 10. Button & status LED

> **Status: implemented and merged (`monospace` PR #5, 2026-08-08,
> `63077d9`).** Verified on real hardware the same day: red/green/blue
> map correctly to D9/D10/D11 with the correct active-low inversion;
> 5/5 deliberate button presses produced exactly one `EVENT BUTTON
> PRESSED` each; sloppy/bouncy presses handled correctly by the
> debounce. `hw_diag`/typed-client support followed in the same pass
> (`sans` PR #7, §9's note) — both sides verified end to end. The rest
> of this document (§1–§9, relays + BMP180) was already built and
> merged earlier (`monospace` PR #2).
>
> Physical part: a 6-contact 16mm momentary metal pushbutton with a
> tri-colour RGB ring (2 switch contacts + R/G/B/C). Wired: button
> contact on D2, RGB cathodes on D9/D10/D11, LED common anode on 5V —
> exactly the pin table in §2. Polarity (§10.3) was confirmed by a
> diode-test, not assumed.

### 10.1 What this board does and does not decide

Same "dumb board" principle as everything else in this document,
stated explicitly here because it's easy to get wrong for a
button-plus-LED: this firmware's *only* jobs are (a) debounce the
physical button contact and report each press as one clean event, and
(b) drive the LED to whatever raw RGB value the host last set. It never
decides what color means what, never blinks on its own, and never
treats a button press as "do something" — that interpretation (Start?
Stop? nothing?) is entirely the Raspberry Pi's, based on whatever
machine state it currently thinks it's in. This firmware doesn't know
or care whether the machine is idle, scanning, or stopped — and doesn't
need to for this task.

### 10.2 Button input

- **Pin D2**, `INPUT_PULLUP`, active-low: the button's other contact
  goes to GND, so idle = HIGH (internal pull-up), pressed = LOW. D2 was
  chosen because it's the Uno's other external-interrupt-capable pin
  (`INT0`) — not required to actually be used as an interrupt for this
  task (simple polling in the main loop is fine at this loop rate), but
  keeps that option open without a future pin reassignment.
- **Debounce:** a simple time-based debounce (e.g. ignore further LOW
  transitions for some tens of milliseconds after the first one) is
  expected — exact constant is an implementation detail, not pinned
  down here, same treatment as the BMP180 sample counts (§6). This is
  signal conditioning, not "automatic behavior" — see §3 point 8.
- **What gets reported:** one `EVENT BUTTON PRESSED` line per
  debounced press edge (the transition into the pressed state). Button
  *release* is not reported — nothing on the Raspberry Pi side needs it
  for this task, and not reporting it keeps this firmware's surface
  smaller. If a future need for release events comes up, that's a
  protocol addition, not something to speculatively build now.
- **No query command.** Same design principle as the rest of this
  protocol (§4): the host finds out about a press only via the
  unsolicited event, there's no way to ask "is the button currently
  pressed."

### 10.3 RGB status LED output

- **Pins D9 (Red), D10 (Green), D11 (Blue)**, PWM (`analogWrite`).
- **Polarity: active-low, confirmed by physical diode-test (ijon,
  2026-08-08).** The button's common LED terminal (`C`) is the common
  anode, tied to 5V; R/G/B are cathodes that sink current to light up.
  Consequence for this firmware: `LED SET <r> <g> <b>` (§5) is a
  *host-facing* value where `0` means that channel fully off and `255`
  means fully on — but since these are cathode pins, "more on" means
  driving the pin *more* LOW, so this firmware's `analogWrite` calls
  must invert the host's value (`analogWrite(pin, 255 - value)`), never
  expose that inversion over the wire, and never assume the relays'
  plain (non-inverted-again) active-low handling applies unchanged here
  — the inversion has to happen somewhere, and it happens inside this
  firmware, not on the host side.
- **Boot default: off**, before the serial interface starts accepting
  commands — §3 point 7. No boot-time exception, unlike the light relay.
- **No local blinking, ever.** A blink pattern is nothing more than the
  host sending alternating `LED SET <r> <g> <b>` / `LED SET 0 0 0`
  calls at whatever interval it wants. This firmware has no concept of
  a "blink mode" and no timer driving the LED on its own — consistent
  with §3 point 2's blanket rule, and deliberately so: a blink cadence
  is a UI decision that belongs on the Raspberry Pi where it's easy to
  change, not compiled into this firmware. Nothing about *what* cadence
  or colors get used is this task's concern — only that raw `LED SET`
  values reach the LED correctly.

### 10.4 Testing — done, including the `hw_diag` extension

**Done (`sans` PR #7, `4d8cdfa`, 2026-08-08):** `hw_diag` now has an
`led <r> <g> <b>` interactive command sending `LED SET`, and its
line-reading loop recognizes the `EVENT ` prefix (alongside its
existing `PRESS ` handling) and prints incoming `EVENT BUTTON PRESSED`
lines timestamped, same as its `PRESS` output. The raw `LED SET ...`
form still falls through to `send_raw` too, so `ERR BAD_ARGS` stays
directly testable. `protocol.rs` classifies `EVENT` lines into their
own `LineKind::Event`; `HwClient` routes them to a new `on_event`
callback (parallel to the telemetry callback) and adds `set_led(r, g,
b)`.

**Verification performed (2026-08-08):** `LED SET` values sent both via
`arduino-cli monitor` and via `hw_diag`'s `led` command lit the ring at
the expected color/brightness both ways; deliberate and deliberately
sloppy/bouncy button presses each produced exactly one `EVENT BUTTON
PRESSED`, observed both in `arduino-cli monitor`'s raw output and in
`hw_diag`'s new handling of it.
