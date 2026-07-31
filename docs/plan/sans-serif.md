# Sans-Serif — Raspberry Pi Application Specification

> Target-state spec for the application running on the Raspberry Pi:
> hardware orchestration, image pipeline, calibration, the auto-scan
> loop, metadata acquisition, archive management, and the touchscreen UI.
> Self-contained: written to be handed to an implementer with no other
> file access, beyond the cross-referenced `reference/` files for
> supplementary derivations (explicitly named where used).
>
> Language: **Rust**. Runs as a single application, one job at a time —
> no message broker, no microservices split. Talks to the ESP32 FOC
> board via the G-code protocol in `bldc-driver.md`, to the Arduino via
> the line protocol in `monospace.md`, and reads the BMP180 pressure
> sensor directly over I2C (no intermediary board).
>
> UI language: English only. No localization infrastructure yet, but
> avoid hard-coding strings in a way that makes adding German later a
> rewrite.

---

## 0. Governing requirement

**The machine is operated by laypeople with no briefing.** Every screen
state handed to the UI must include an explicit, plain-language
instruction of what to do next — never a bare preview image or raw data
with no accompanying instruction. This applies to every screen listed
in §8.

---

## 1. Hardware client layer

### 1.1 FOC-board client

Rust client speaking the G-code protocol defined in `bldc-driver.md`
over USB-serial, direct connection to the RPi. Exposes to the rest of
this application:

- `arm()` / `disarm()` (`M3`/`M5`)
- `home() -> zero_mm` (`G28`) — only ever called after the UI has
  obtained explicit user confirmation (§8, screen 1)
- `move_to_top(target_mm)` (a plain `G0` to a small absolute value near
  the homed-zero position — there is no dedicated board-side command for
  this, see `bldc-driver.md` §10) — refused upstream (by the board) if
  not homed; this client surfaces that as a distinguishable error, not a
  generic failure. Used both by the touchscreen recovery control (§8)
  and to park the box at job end, before `disarm()`.
- `move_fast(target_mm)` / `move_slow(target_mm)` (`G0`/`G1` — both
  always absolute; there's no relative-move mode in this protocol, see
  `bldc-driver.md` §6)
- `complete_page_turn(target_mm, retreat_mm) -> final_position_mm`
  (`G73`) — runs the board's entire upward turn-completion motion
  (embedded retreat, then climb to `target_mm`) in one call; while it's
  in flight, this client polls `status()` in a loop so the caller (§5)
  can react to position crossings (e.g. trigger the Arduino turn-blower
  around 80% of page width) without waiting for the call to return.
  Can be cancelled mid-flight by calling `stop()` — the caller (§5)
  does this when its own concurrent pickup-success check fails; the
  board has no concept of pickup success/failure itself, it's purely a
  motion command. Does **not** include the following descent — the
  *next* `touchdown()` call handles that.
- `touchdown(press_value) -> {stop_position_mm, compression}` (`G30`)
- `resume()` (`M24`) — must only be called after `capture_pair()` (§2)
  has actually succeeded, never just falling through in sequence
- `stop()` (`M112`)
- `status() -> {state, position_mm, torque, homed}` (`?`)
- Calibration triggers (`M40`/`M41`/`M42`) — exposed for a dedicated
  calibration entry point, not part of the normal scan flow (§8 has no
  screen for this yet; needs one)
- Listens continuously for unsolicited `fault ...` messages and
  surfaces them to whatever is driving the current operation
  (§5's Auto-Scan loop, in particular) as equivalent to a Stop.

**Safety invariant this client's callers must uphold:** the suction box
must never move downward while the vacuum motor is running, with
exactly one exception — `complete_page_turn()`'s embedded retreat step.
Every other downward move must have vacuum and both blow units switched
off first. This is currently enforced by caller discipline (§5), not by
the board itself.

### 1.2 Arduino client

Rust client speaking the line protocol defined in `monospace.md` over
USB-serial (a second, independent serial connection from the FOC
board's). Exposes: `vacuum_on()`/`vacuum_off()`,
`separation_fan_on()`/`separation_fan_off()`,
`turn_blower_on()`/`turn_blower_off()`, `light_on()`/`light_off()`/`light_auto()`,
`all_actuators_off()`. **No pneumatic-pressure-sensor read** — that
sensor only exists if the (not yet decided) pneumatic rebuild happens,
see `monospace.md` §7.

### 1.3 BMP180 driver

Direct I2C read (e.g. via a `smbus`-equivalent crate), standard Bosch
calibration/read procedure. Exposes `read_ambient_pressure()` and
`read_differential_pressure()`. No Arduino or ESP32 involvement — same
process that commands the FOC board also reads pressure, so pickup
detection never needs to correlate two independently-clocked serial
links.

---

## 2. Camera capture & preview service

- **`capture_pair() -> (raw_left, raw_right)`** — triggers both UVC
  cameras via v4l2 for a synchronized shot. **Applies rotation here**
  (fixed, hardware-measured constant per camera, may differ left vs.
  right if mirror-mounted) — callers never see un-rotated images. If a
  camera fails to return a frame (disconnected, timeout, corrupt read):
  retry once, then surface a hard failure to the caller. This runs
  unattended across potentially hundreds of shots per book; a silent
  bad/missing image is worse than a loud failure.
- **`make_preview(raw_image, crop_rect, target_size) -> preview_image`**
  — crop-then-scale, used identically by the calibration preview, the
  live full-page preview, and the live zoom preview (§8). `crop_rect` is
  always expressed in raw-image pixel space (post-rotation) — one
  consistent convention for every caller, so tap-coordinate mapping
  (§4) never has to guess which space it's in.

---

## 3. Page-number recognition service

- **`recognize_page_number(raw_image, crop_rect) -> Option<String>`** —
  crops the given rectangle and runs local `tesseract` on it. **Must
  not** restrict to a digit-only whitelist — front-matter pages may use
  roman numerals (i, ii, iii...), which would otherwise all read as
  `None`. Trims whitespace; empty result → `None`. No further
  validation or gap-tolerance logic here — that's the whole-book
  completeness check, owned by the Job/Archive Manager (§7).
- Stateless, synchronous, single-page in/out. Used per-attempt by the
  Auto-Scan loop (§5) and, for re-scans (§9), by the same operation
  again.

---

## 4. Setup-calibration controller

Runs once per book, before Auto-Scan starts. Produces
`{page_width_mm, page_number_crop: {left, right}}`, handed to the
Job/Archive Manager (§7) and to the Auto-Scan controller (§5) for this
session. The two images captured here are **ephemeral** — used to build
the tap-preview, then discarded; they get no `sequence_number` and never
reach the archive.

**Sequence:**

1. UI prompts the user to open the book to a page with visible page
   numbers and place it in the cradle; waits for a "ready" signal.
2. `touchdown()` (no press-value override needed beyond the default) —
   lowers onto the page.
3. `capture_pair()` — the calibration shot, box stationary. Hold
   `calib_img_left`, `calib_img_right`.
4. `resume()` — required before any further motion (the board is
   holding).
5. `read_ambient_pressure()` → `p_baseline`, before vacuum engages.
6. `vacuum_on()`, `separation_fan_on()`.
7. **Detection #1 (pickup-success drop)** — the same check every normal
   page-turn does (§5 step 8): shortly after vacuum engages,
   `read_differential_pressure()` should show a clear drop vs.
   `p_baseline`. No drop = pickup failure, same error condition as a
   normal flip-cycle failure (exact retry/abort handling for this
   calibration context is an open point, not specified further here).
8. Once pickup is confirmed: ascend via a **distinct monitored-ascent
   move** (not `move_to_top()` — a plain vertical move, watching
   pressure as it goes, with no page-separation wiggle and no return
   trip; this is a one-way measurement, not a flip cycle).
9. **Detection #2 (page-separation rise, calibration-only)** — watch for
   the differential pressure suddenly rising back toward baseline once
   the page's trailing edge clears the suction box. Exact detection
   algorithm needs real hardware data to finalize; a starting
   placeholder is "value crosses back above a threshold, having been
   steady below it since step 7." A small compensating offset
   (~velocity × sensor lag) may need to be added to the raw detected
   position — needs empirical determination.
10. Record the box's position (mm, plus any lag offset) at detection
    #2's point as `page_width_mm`.
11. `vacuum_off()`, `separation_fan_off()`. Box ends at the top; the
    user manually pages the book back to its actual first page before
    Auto-Scan starts (this calibration page was arbitrary, not
    necessarily page 1).
12. Return `{page_width_mm, calib_img_left, calib_img_right}`.

**Tap-interaction (coordinate mapping):**

1. `make_preview()` on both calibration images, using the same fixed
   crop-then-scale rectangle as the live full-page preview.
2. UI prompts "tap the page number on the left page" → reports
   `tap_point_left` in **preview-display pixel space**.
3. Convert to raw-image space using the same `crop_rect` from step 1:
   ```
   raw_x = crop_rect.x + tap_point.x * (crop_rect.width / preview_display_width)
   raw_y = crop_rect.y + tap_point.y * (crop_rect.height / preview_display_height)
   ```
4. Center a default-size rectangle (fixed constant, needs empirical
   tuning on the real machine — large enough to comfortably contain the
   digits regardless of tap precision) on `(raw_x, raw_y)`.
5. Re-tapping the same side before confirming **replaces** the
   rectangle, doesn't add a second one. Resizing is not built.
6. Repeat for the right side.
7. Return `{page_number_crop: {left: rect_left, right: rect_right}}`.

Full derivation and the open tuning points: `reference/process-assumptions-audit.md`.

---

## 5. Auto-scan flip-cycle controller

Work happens in **page slots**, each spanning one or more pickup
**attempts**. A failed pickup retries locally within the same slot; it
never discards the slot's photo or restarts from the top.

### 5.1 Safety invariant (binding on every step)

The suction box must **never** move downward while the vacuum motor is
running, with exactly **one** exception: `complete_page_turn()`'s
embedded retreat step (step 9 below). Every other downward move must
have vacuum and both blow units switched off *first*.

### 5.2 Single pickup attempt

Every call is identical — this operation doesn't know or care whether
it's attempt 1, 2, or 3 for the current slot; that bookkeeping is
entirely the loop driver's (§5.3) job. Inputs: `page_width_mm`.

1. `read_ambient_pressure()` → `p_baseline`. Read fresh on every
   attempt, before any pneumatics for this attempt engage.
2. `touchdown()` — a real closed-loop descend-until-contact every
   attempt, including retries; never a move to a remembered position
   (book thickness/binding varies page to page).
3. If the light switch is in Auto mode: `light_on()`.
4. `capture_pair()` — both cameras, box stationary at the
   touchdown/compressed position. Taken on **every** attempt, not just
   the first — every attempt's photo gets OCR'd (step 3, cross-checked
   against attempt 1's reading) even though only attempt 1's photo ends
   up archived. Hold `img_left`, `img_right`.
5. `resume()` — required before any of the following steps; the board
   is holding after touchdown and refuses to move otherwise.
6. `separation_fan_on()`.
7. If the light switch is in Auto mode: `light_off()`.
8. `vacuum_on()`.
9. Call `complete_page_turn(target_mm, retreat_mm)` (§1.1, `G73`) —
   this starts the board's entire remaining upward motion for the
   attempt immediately; there is no separate move or command for the
   pickup-success check itself, only a pressure reading the RPi takes
   on its own. **While this call is in flight**, run two things
   concurrently on the host side:
   - **The pickup-success check.** Not a single point-in-time read —
     `read_differential_pressure()` is polled **continuously**
     throughout `complete_page_turn()`'s execution (the same monitoring
     loop already polling `status()` for relay timing). Expected
     physical behavior, two distinct readings to tell apart:
     - Switching the vacuum pump on causes **some** drop below
       `p_baseline` even with **no** page actually held — air still
       flows (leaks) through the suction cups when nothing is sealing
       them, so the pump can't build much vacuum. Call this shallow
       reading `p_leak` (not a fixed constant — needs empirical
       characterization on real hardware, but expected to exist).
     - A **successful** pickup (page sealing the suction cups) lets the
       pump build a real vacuum — the reading should sit **below**
       (more negative / further from ambient than) `p_leak`, not just
       below `p_baseline`. The threshold distinguishing "shallow,
       pump-on-but-nothing-held" from "deep, actually sealed" is the
       one that needs empirical tuning (§6's `threshold_mbar_used`
       logging exists for exactly this) — checking only "any drop from
       `p_baseline`" isn't sufficient, since `p_leak` alone would
       already satisfy that.
     - **Two distinct failure conditions**, either one triggers the
       same abort below:
       1. The reading never drops past the success threshold at all
          within an early window (page was never picked up) — exact
          window/trigger not decided.
       2. The reading **was** past the success threshold (pickup looked
          fine) and then **suddenly jumps back up toward `p_baseline`**
          at any point during the ascent — the page was picked up
          initially but lost its seal partway through the lift. This
          can happen anywhere during `complete_page_turn()`'s
          execution, not just near the start, which is exactly why the
          check has to keep running for the whole call, not just once
          early on.
     - **On success (reading stays past the threshold, no sudden
       return):** do nothing — let `complete_page_turn()` keep running
       toward its target, uninterrupted.
     - **On either failure condition:** call `stop()`
       (`M112`) to abort the in-flight `complete_page_turn()`
       immediately, wherever the box happens to be. Then
       `separation_fan_off()`, `vacuum_off()`, then a fresh
       `touchdown()` (`G30`) for the retry — it descends correctly from
       wherever the abort left the box, no matter where that was.
       Return `{pickup_result: "failure", images: {img_left, img_right},
       ambient_mbar: p_baseline, differential_mbar: p_baseline - p_check}`
       (`p_check` being whichever reading triggered the failure) —
       images are returned even on failure (needed for the
       cross-check, and in case this was attempt 1). **Stop here on
       failure** — step 10 doesn't run.
   - **Relay timing**, independent of the pickup check above: poll
     `status()` in a loop and, based on the reported position, drive
     the Arduino relays at the right moments (the board has no
     awareness of any of this, per `bldc-driver.md` §6.1):
     - `separation_fan_off()` once past the ~60–70%-of-page-width zone
       (early in this call, right after the embedded retreat).
     - `turn_blower_on()` once past ~80% of page width. (This
       move-then-engage timing is the simpler reading of the source
       process description; needs validating against real page-turn
       behavior — the blower may need to fire earlier/concurrently.)
     - `vacuum_off()` — timing not fixed: natural page separation
       likely happens near 100% of page width on the way up, but
       whether this should tie to that detection or simply happen once
       `complete_page_turn()` returns (box now held at the top of this
       attempt's motion) isn't decided.

   **Why this has to be continuous, not a single checkpoint read:**
   failure condition 2 above (page lifted successfully at first, then
   lost partway through the ascent) can only be caught by watching the
   whole way up — a single stationary check-then-decide move (read
   once, only send `G73` afterward if it looks good) would catch
   condition 1 fine, but would have no way to notice a mid-lift loss at
   all, since nothing would be watching once `G73` started. A moving box
   may add some airflow/vibration noise to the differential-pressure
   reading compared to a stationary read — worth validating on real
   hardware, since it affects how tight the success threshold can be set
   — but the continuous approach isn't optional here, it's what the
   failure mode requires.
10. Return `{pickup_result: "success", images: {img_left, img_right},
    ambient_mbar: p_baseline, differential_mbar: p_baseline - p_deepest,
    touchdown_position, touchdown_compression}` (last two from step 2).
    `p_deepest` is the lowest (furthest-below-`p_baseline`) reading
    observed during the continuous monitoring above — a single
    representative value for logging (§6), not any one specific
    instant.
    **The box is held stationary** at wherever `complete_page_turn()`
    stopped (110–120% of page width, or the fallback) when this
    returns — it does not reverse or descend on its own. The *next*
    slot's `touchdown()` call is what brings it back down; while that
    call is in flight, poll `status()` again to catch the box passing
    back down through ~90% of page width and call
    `turn_blower_off()` at that point — the blower's on-time spans the
    end of *this* attempt's `complete_page_turn()` and the start of the
    *next* slot's `touchdown()`, not just this attempt in isolation.

### 5.3 Loop driver (control flow, recording, Stop handling)

For each page slot (identified by the `sequence_number` it will occupy
once archived):

1. Assign the slot's `sequence_number` now, at the start of attempt 1.
   Left camera → even number, right camera → odd number, continuous
   across the whole book (never reassigned, never reused except by
   retries of the same slot).
2. Call the single-attempt operation (§5.2), attempt 1. Keep this
   attempt's `images` result in hand regardless of outcome — this is
   the photo that ultimately gets archived under this slot's
   `sequence_number`, no matter which attempt's pickup actually
   succeeds.
3. Run page-number recognition (§3) on this attempt's images. Emit this
   attempt's `ambient_mbar`/`differential_mbar` for live display (§8)
   and logging (§6) — every attempt logs its pressure values, not just
   the first.
4. **On success:** persist attempt 1's images (held since step 2) under
   this slot's `sequence_number`, with attempt 1's recognized page
   number (not necessarily this attempt's, if they differ — attempt 1's
   OCR reading describes the archived photo). Reset the pickup-failure
   counter. Advance to the next slot.
5. **On failure:** a mismatch between this attempt's recognized number
   and attempt 1's is expected OCR noise, not a real page change (a
   failed pickup-success check aborts `complete_page_turn()` early,
   well before the page could have separated and turned) — no special
   handling needed
   beyond keeping attempt 1's reading as authoritative. Increment the
   failure counter; at 3, trigger the failure dialog (§8) — the box
   moves to the real move-to-top position only as part of that abort,
   not as part of per-attempt failure handling (which stays at a fresh
   touchdown). Discard this attempt's images unless it was attempt 1
   (whose images stay held). If under the limit, retry (back to step 2,
   same `sequence_number`).

**Stop handling:** must interrupt promptly, including mid-attempt —
requires the FOC client's and Arduino client's immediate-stop/
all-actuators-off operations. An unsolicited hard-stop fault from the
FOC board (§1.1) is treated exactly like a user-pressed Stop. On Stop,
fault, or a 3-failure abort: halt, leave the machine in the stopped
state for the recovery controls (§8). A slot that was mid-attempt when
stopped has **no** archived image — resuming after a Stop starts that
slot over from a fresh attempt 1, never from wherever the interrupted
attempt was.

---

## 6. Live diagnostics & pressure log

- **Live display (during Auto-Scan):** current ambient pressure
  (pre-vacuum baseline, reused from step 1 of §5.2, no new measurement
  point) and the differential values of the last three flip cycles
  (N-2, N-1, current). Shown directly in the main scan screen (§8), not
  a separate debug screen.
- **`pressure-log.jsonl`** — one entry per pickup **attempt** (matching
  §5.3 step 3 — every attempt is logged, not just the one whose photo
  gets archived), separate per-job file, uploaded alongside the
  archive. Fields:
  `ambient_mbar`, `differential_mbar`, `pickup_result`,
  `threshold_mbar_used`, `touchdown_position`, `touchdown_compression`.
  Purpose: deriving real pickup-success/-failure thresholds empirically
  across many books, and correlating stop position/press force/
  pneumatic pressure for debugging — kept separate from
  `metadata.json` (§7) since it has a different consumer and lifecycle
  (cross-book analysis, not per-book archival data).

---

## 7. Job/archive manager

- **Creates the job ID** at job start — assigned once, immutable for
  the job's lifetime, never derived from editable fields (title, ISBN).
- **Assembles `metadata.json`** as data arrives from calibration (§4),
  Auto-Scan (§5), and metadata capture (§8's flow) — not necessarily
  all at once; Auto-Scan emits per-page, not batched. Schema:
  - `page_width_mm`, `page_number_crop: {left, right}`
  - `pages[]`: one entry per archived page, each with
    `sequence_number`, `file`, `recognized_page_number` (string or
    `null`), `status`
  - full schema and rescan-related fields: `reference/rescan-flow.md`
- **File naming:** filenames are the continuous `sequence_number`
  (e.g. `0024.jpg`), left captures even, right captures odd — never
  derived from upload order or a timestamp, never reassigned. Enables
  a future single-file replace (re-scan) without renumbering anything
  else in the archive.
- **Writes `pressure-log.jsonl`** (§6).
- **Uploads the finished archive** to the Z2 storage-machine share on
  job completion.
- **Whole-book completeness/gap check** — comparing every page slot's
  `recognized_page_number` to flag a likely-missing page, with
  tolerance for roman/arabic switches and unnumbered plates — is owned
  here (natural fit: this is the component that already assembles the
  full `pages[]` array, and the check needs to run RPi-side). Detection
  algorithm not yet specified.
- **Upload failure handling** — not yet specified beyond: at minimum,
  an error message on the touchscreen with a manual retry option (§8).
- **Explicitly not responsible for:** deciding *what* the metadata
  values are (only persists what other components hand it), any image
  processing, any UI.

---

## 8. Touchscreen UI shell (serif)

Pure presentation + input capture + navigation — no process logic of
its own. Every screen below carries an explicit, plain-language
instruction (§0).

### 8.1 Happy-path screen flow

1. **Boot/home.** "Machine ready? Box will move up" → explicit confirm
   required before homing runs (never automatic on power-on). Only
   after homing succeeds does the start screen appear. Homing failure
   needs its own error path (§8.2).
2. **Cover capture.** Box moves to top. "Place the closed book, front
   cover up, on the right side" → confirm → capture (right camera).
   "Now the back cover, on the left side" → confirm → capture (left
   camera). Both photos always taken, unconditionally — thumbnail
   source for the web portal regardless of what happens next.
3. **ISBN check.** "Does the book have an ISBN barcode? Check the
   cover, spine, or first/last pages" → three branches: barcode found
   (cover case reuses step 2's photos directly, no reposition; interior
   case: open to that page, fresh shot, decode via a barcode-decoding
   library — distinct from the tesseract text-OCR in §3) / printed but
   no barcode (on-screen numeric keyboard) / none found (skip to step
   5).
4. **Metadata confirmation.** Show the found/guessed candidate:
   "Is this correct?" [Confirm] / [Enter manually].
5. **Manual metadata form** (fallback only). On-screen keyboard,
   clearly labeled fields, then search-after-typing (not live
   autocomplete) against the same book-database lookup used in step 3,
   showing candidates for the user to confirm or dismiss.
6. **Calibration.** "Open the book to a page with a visible page
   number, in the middle, and place it in the cradle" → [Ready] →
   preview shown → "Tap the page number on the left" → tap → "...now
   the right" → tap → confirm.
7. **Auto-Scan start.** "Ready to scan?" [Start].
8. **Auto-Scan live screen.** Full-page preview + zoom preview (§2's
   `make_preview`, unscaled 100% crop) + page counter + live pressure
   diagnostics (§6) — [Stop] always visible and reachable.
9. **Scan-completion confirmation.** "Finished scanning this book?"
   [Yes, done] / [Keep scanning].
10. **Upload/finalization.** Progress indicator, then a success
    confirmation (failure case is §8.2's).

### 8.2 Error, recovery, and interruption screens

1. **Pickup-retry visibility.** A failed pickup attempt retries
   automatically (§5.3) — show a brief visible status during a retry
   ("Retrying this page...") rather than silence, so a layperson
   doesn't assume the machine is stuck.
2. **3-failure choice dialog.** Plain-language explanation ("This page
   couldn't be picked up. Please check it's lying flat.") followed by
   three explicit choices: **[Try again]** (resets the failure counter,
   3 further attempts on the same slot), **[Turn page manually]** (user
   physically turns the page, confirms via button, Auto-Scan resumes;
   attempt 1's already-held photo for this slot gets archived as-is,
   since it's a valid photo of the page as it was before the manual
   turn), **[Stop job]** (ends Auto-Scan, leads into the Stop/recovery
   screen below).
3. **Stop/recovery screen.** Shown after any Stop (user-initiated or
   "Stop job" above). Explains the state in plain language, not a bare
   button row: **[Move to top]** (primary, with a short explanation —
   moves the box up so the book can be checked/freed) and
   **[Move down]** (secondary, jog control). A way back into the flow
   once ready — resume, never silent auto-continue.
4. **Upload failure.** Error message + [Try again], never a silent
   failure or dead end.

### 8.3 Metadata-flow database lookup (shared sub-capability)

One lookup capability, two query modes — exact ISBN lookup and
title/author search — against Open Library (no key) with Google Books
as secondary (key required). Returns structured metadata (title,
author, publisher, year, cover URL if available) or no-match. Built
once, shared by the ISBN path, the cover-LLM fallback path, and the
manual-form verification step (§8.1 steps 3–5) — not reimplemented per
caller.

**Cover-LLM fallback:** when the ISBN path produces no result, send the
step-2 cover photos to an image-recognizing LLM (local AI hardware) to
extract a candidate title/author, then run that candidate through the
title/author search mode. Always surfaced to the user for confirmation
(§8.1 step 4) — never auto-accepted, since it's a guess, unlike a
direct ISBN hit.

---

## 9. Rescan support (data-model groundwork only — flow itself not built yet)

The current MVP data model must not block adding, later, a flow where a
user marks a defective page in the web portal and rescans it at the
machine. Concretely, already required by §7's schema above:

- Stable per-page file identity via `sequence_number` (§7) — a future
  rescan overwrites one file, renumbers nothing.
- `recognized_page_number` persisted per page (§7), not just used
  transiently.
- Calibration parameters (`page_width_mm`, `page_number_crop`)
  persisted per book (§7), not kept only as runtime state.

Full flow design (on-device job list, rescan-prompt loop, web-portal
gallery, archive replace-on-upload): `reference/rescan-flow.md` — none
of it is part of this application's MVP scope yet.

---

## 10. Cover-photo post-processing (not MVP scope)

The two cover photos from §8.1 step 2 are stored raw for MVP. A later
version needs to rectify them (corner detection, perspective correction,
crop, color-correct) — different requirements from normal-page
post-processing since the book is closed/rigid with no fixed reference
rectangle. Not part of this application; see
`reference/cover-photo-postprocessing.md`. This is VM-side (Z2)
post-processing work, out of scope for this RPi application entirely.
