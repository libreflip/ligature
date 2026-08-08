# Tasks

> Format per `CLAUDE.md` (Softwarearchitekt-Modus), Abschnitt 2: each task
> has a clearly bounded scope, is independently testable, and has a
> "done when" criterion. Grouped by architecture decision (`architecture.md`).
> Tasks under "parked" are fully specified but not yet actionable — they
> depend on work (or an ijon decision) that hasn't happened yet.

## Board/component mapping (AD-006)

Per-task detail and history stay here; the clean, current-state target
spec for each board lives in its own top-level file. This table is the
index between the two.

| Board/file | Tasks |
|---|---|
| [`ligature.md`](ligature.md) (MKS ESP32 FOC) | T32 (firmware), T20 (protocol design half only — RPi-side client half is `sans-serif.md`) |
| [`monospace.md`](monospace.md) (Arduino) | T22 (firmware/protocol design half only — RPi-side client half is `sans-serif.md`), T21 (firmware half only — RPi-side client half is `sans-serif.md`), T34 (firmware half only — RPi-side client+logic half is `sans-serif.md`) |
| [`sans-serif.md`](sans-serif.md) (Raspberry Pi, backend + UI) | T1, T2, T3, T4, T6, T7, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20 (RPi client half), T21 (RPi client half), T22 (RPi client half), T23, T24, T25, T26, T27, T28, T29, T30, T31, T35 |
| Out of scope for these three files (Z2/Z3, VM-side, not machine software) | T5, T8, T33 |

**Notes:**
- T20, T21, and T22 are the tasks split across two files: the wire
  protocol/firmware behavior is now specified in `ligature.md`/
  `monospace.md` respectively (per AD-006); the RPi-side client
  implementing against that protocol is specified in `sans-serif.md` §1.
  T21 was originally RPi-direct-only (before AD-001's 2026-08-01
  reversal moved the BMP180 back to the Arduino) — now split like T20/T22.
- T9/T10 (recovery/jog controls) are UI+client logic on the RPi side
  (`sans-serif.md`), even though the motion they trigger is executed by
  the FOC board — the button and its state logic live on the RPi.
- T34/T35 follow the same T20/T21/T22 split pattern (added 2026-08-08,
  `architecture.md` AD-009): T34 is the Arduino-side firmware
  (`monospace.md` §10), T35 is both the RPi-side Arduino-client
  additions (`sans-serif.md` §1.2) *and* the indicator/button-routing
  logic (`sans-serif.md` §11) — unlike T20/T21/T22, that logic piece
  has no firmware-side counterpart at all (§11 is pure RPi-side
  decision-making over already-existing state), so there's no third
  file to split it into.

---

## AD-002: Nachscan-Flow für defekte Einzelseiten

Full background: [`reference/rescan-flow.md`](reference/rescan-flow.md).
Decision: v2 feature, but §4.1–4.4 below are MVP-side groundwork so v2
doesn't require a data migration later.

### MVP-side preparation (actionable as part of MVP Z1/Z2 build)

#### T1 — Stable per-page file identity in the capture/archive pipeline
- **Scope (revised, ijon, 2026-07-26 — supersedes the original
  spread-index+side design):** when the RPi captures a page during the
  auto-scan loop and when the resulting image is written into the Z2
  archive, the filename is derived from a single, continuous
  `sequence_number` assigned once at capture time, never reassigned —
  **not** a spread-index+side pair. No separate left/right field:
  parity encodes the side — left-camera captures get even numbers,
  right-camera captures get odd numbers, matching standard book
  pagination (even = left/verso, odd = right/recto). Not from upload
  order or a timestamp. **Refined twice on 2026-07-26 — second pass
  supersedes the first.** First pass: assigned only on pickup success,
  failed cycles discarded entirely. **Corrected (ijon, same day): that
  was based on a flawed retry design** (see T24's changelog) where a
  failed pickup restarted the whole cycle from scratch. Actual design:
  a **page slot** gets its `sequence_number` at the start of its first
  pickup attempt; retries for that slot (if the first attempt's pickup
  fails) reuse the same `sequence_number` and don't consume new ones —
  the slot's archived image is always the **first** attempt's photo,
  regardless of which attempt's pickup actually succeeds. See
  `tasks.md` T23/T24 for the full attempt-vs-slot mechanics.
- **Done when:** a test-scanned book's archive folder contains files
  named by continuous sequence number (e.g. `0024.jpg`, `0025.jpg`),
  left captures even, right captures odd; manually overwriting one
  such file (simulating a future re-scan) requires no other file in
  the archive to be renamed or renumbered.
- **Depends on:** MVP Z1 capture loop, MVP Z2 archive writer (both
  under active MVP design — this task only constrains their file
  naming, doesn't require a separate work stream).

#### T2 — Persist recognized page number per page
- **Scope:** the RPi-local completeness check (fixed-rectangle crop +
  `tesseract`, see `reference/process-assumptions-audit.md`) already
  computes a recognized page number (or none) per page for the gap
  check. That value must be written into the book's persisted metadata
  record, per page, not just consumed transiently.
- **Done when:** the metadata file for a test-scanned book lists, for
  every page entry, a `recognized_page_number` field (string or
  `null`), and its values match what the completeness-check log showed
  during that scan.
- **Depends on:** MVP completeness-check implementation, MVP metadata
  file format (see open point #7 in `reference/z1-z3-triage-overview.md`
  — this task partially resolves that open point by fixing the per-page
  schema; book-level metadata fields beyond what's listed here remain
  open).

#### T3 — Persist per-book calibration parameters
- **Scope:** the combined setup-calibration flow (page-width +
  page-number crop rectangles, see `reference/process-assumptions-audit.md`)
  currently produces values consumed live during one scan session.
  These must additionally be written into the book's persisted metadata
  record (page width in mm, crop rectangle for left camera, crop
  rectangle for right camera) — not kept only as RPi runtime state.
- **Done when:** after a normal MVP scan run and upload, the resulting
  metadata file on the storage share contains the calibration values
  from that session; feeding those values back into the crop code path
  against a manually supplied test image reproduces the same crop
  region used during the original scan.
- **Depends on:** MVP calibration flow, MVP metadata file format (same
  as T2).

#### T4 — Stable job/book identifier
- **Scope:** every scan job (book) gets an identifier assigned once at
  job creation, immutable for the job's lifetime, not derived from any
  field that could later be edited (e.g. not derived from title/ISBN
  metadata, which may be corrected after the fact).
- **Done when:** two separate test scans produce two distinct IDs that
  don't collide; each job's own metadata file records its ID; editing
  that job's title/metadata afterward does not change the ID.
- **Depends on:** MVP job/metadata record design (needed regardless of
  this feature — the MVP Web-Portal job list already needs to
  distinguish jobs from each other, see `reference/z1-z3-triage-overview.md`
  item 16).

---

### Parked (v2 — fully specified, not actionable until MVP ships)

These are described in detail in `reference/rescan-flow.md` §6 and not
repeated here in full. Listed so the dependency chain is visible:

- **T5 — Web-Portal per-page image gallery + "Seite nachscannen" button.**
  Depends on T1–T4 (needs the MVP data model in place) and on a design
  decision about overlap with the separately-deferred "interactive
  correction UI" cluster (`reference/z1-z3-triage-overview.md`, "großes
  Vier"-Cluster) — worth designing together, not necessarily as two
  separate galleries. Not scoped in detail yet.
- **T6 — Z1 on-device job list ("Projekt laden").** Depends on T4 (job
  IDs to list) and a new small VM-side query ("jobs with pending
  rescan flags") that doesn't exist yet.
- **T7 — Z1 rescan-prompt loop + manual single-shot capture reuse.**
  Depends on T2/T3 (needs recognized page numbers and calibration
  rectangles retrievable from the VM). Fallback for a flagged page with
  no recognized number is resolved (`reference/rescan-flow.md` §3:
  neighbor interpolation + thumbnail) — no longer a blocker.
- **T8 — Z2 replace-in-archive on new upload.** Depends on T1 (stable
  filename to overwrite) and T2 (metadata `status` field transition).
- **T33 — Cover/back photo rectification (corner detection, perspective
  correction, crop, color-correct).** New (2026-07-29). Not scoped in
  detail — see
  [`reference/cover-photo-postprocessing.md`](reference/cover-photo-postprocessing.md)
  for the full write-up of why this differs from normal-page
  post-processing (closed/rigid book, no fixed reference rectangle) and
  what it should cover. Depends on T15 (produces the raw photos this
  consumes) and, loosely, on however the normal-page geometry pipeline
  (`reference/image-geometry-correction-order.md`) ends up implemented,
  since some of it (fine rotation, color-correction) may be shared code.

---

## AD-003: Not-Stop-Recovery und Live-Druckdiagnose (MVP)

Full background: [`reference/legacy-process-docs-triage.md`](reference/legacy-process-docs-triage.md).
T9–T14 are all confirmed MVP (ijon, 2026-07-26).

#### T9 — Move-to-Top recovery button
- **Scope (corrected 2026-07-29 against the firmware spec — this is
  *not* the same operation as power-on homing, despite the previous
  version of this task saying so):** touchscreen control, available
  whenever the machine is stopped (after Stop/emergency-stop or
  generally at rest). Moves the suction box toward the top of its
  travel, without engaging vacuum/blower/fan — but **stops a small
  margin short of the physical endstop switch**, it does not touch it
  (the firmware treats an actual endstop hit outside of homing as a
  hard fault, see T20). **Correction (2026-07-29): this is *not* the
  same primitive T27 uses.** T27's up-move is a distinct thing — an
  ascent that continues while monitoring differential pressure and
  stops on a detected event (the page separating), not a move to this
  fixed short-of-endstop target. Established as two different
  operations; don't conflate them again.
  **Requires the machine to already be homed** (T20's `touchdown()`/
  homing distinction) — if homing hasn't run yet this session, T20
  reports a distinguishable "not homed" error, which this control must
  surface to the user (e.g. prompt to run the boot-time homing step)
  rather than fail silently or generically. Does **not** auto-resume
  Auto-Scan afterward.
- **Done when:** triggering Stop mid-Auto-Scan, then pressing
  "Move to Top", moves the box up to just short of the endstop without
  touching it and without engaging vacuum/blower/fan, and the machine
  remains in a stopped, non-resuming state afterward; pressing the
  button before the machine has been homed this session shows a
  distinct "not homed" message instead of a generic error or silent
  no-op.
- **Depends on:** T20 (FOC-board protocol client, provides the
  move-to-top primitive and the "homed" state check).

#### T10 — Move-Down jog control (secondary priority)
- **Scope:** manual control for moving the box down under direct user
  control, independent of the automatic flip-cycle sequence. Lower
  priority than T9 — present in MVP, but not the primary recovery path.
- **Done when:** the control exists and moves the box downward on
  demand, without triggering the automatic flip-cycle side effects.
- **Depends on:** T20 (same underlying motor-control primitive as T9).

#### T11 — Live pressure diagnostics in the main scan screen
- **Scope:** during Auto-Scan, display (a) current ambient pressure,
  measured pre-vacuum by reusing the existing baseline read
  (`scan-process.md` step 8.1 — no new measurement point), and (b) the
  pickup-differential values (reusing step 8.9's measurement) for the
  last three flip cycles (N-2, N-1, current). Shown directly in the
  main scan screen, not a separate debug screen (ijon, 2026-07-26).
- **Done when:** while auto-scanning a test book, the touchscreen shows
  a current ambient-pressure reading and three previous-cycles'
  differential values that update after each flip, visible without
  navigating away from the scan view.
- **Depends on:** BMP180 I2C driver on RPi (AD-001), Auto-Scan loop
  implementation.
- **Optional idea, not committed (ijon, 2026-08-01):** instead of (or
  in addition to) the numeric N-2/N-1/current differential values
  above, show the pressure-over-time curve of the suction box during
  the current/most recent flip cycle as a graph. Explicitly **not**
  requested as a firm MVP requirement, and it's still open whether ads
  (auto-detected-separation / pickup-detection logic) even counts as
  MVP scope in the first place — only worth doing here if it drops out
  essentially for free from whatever pressure-sampling/plotting T11
  already needs, otherwise this stays parked for v2. No implementation-
  effort estimate exists yet, so no MVP/v2 call has been made either
  way.

#### T12 — Pressure diagnostics log (`pressure-log.jsonl`) — confirmed (ijon, 2026-07-26)
- **Scope:** persist, per flip cycle, `ambient_mbar` /
  `differential_mbar` / `pickup_result` / `threshold_mbar_used` as a
  separate per-job log file (not merged into the AD-002 book
  `metadata.json`), uploaded alongside the job's archive so
  pickup-success/-failure thresholds can be derived empirically later
  across many books/sessions. **Extended 2026-07-29:** also log
  `touchdown_position` and `touchdown_compression` from T23's
  `touchdown()` result, for the same per-flip-cycle entry — the whole
  point is correlating stop position, press force, and differential
  pressure together for debugging/analysis, not just the pressure side
  alone.
- **Done when:** a completed test scan job's uploaded archive contains
  a pressure-log file with one entry per flip cycle, matching the
  number of captured pages, each entry's `threshold_mbar_used` matches
  the value actually in effect on the firmware at scan time, and each
  entry also carries that cycle's touchdown position and compression
  value.
- **Depends on:** T11 (same measurement points), T20/T23 (touchdown
  position/compression reporting).

#### T13 — Zoom live preview (100% crop) — confirmed (ijon, 2026-07-26)
- **Scope:** alongside the already-confirmed scaled full-page preview
  during scan, show a second, unscaled, tightly-cropped live preview
  ("zoom"), reusing the crop+scale pipeline already needed for the main
  preview. Intended to let the user catch blur/dust/reflections live,
  reducing reliance on the AD-002 re-scan flow.
- **Done when:** while auto-scanning a test book, the touchscreen shows
  both the scaled full-page preview and a separate, unscaled 100%-crop
  preview of the last shot, both updating after each flip cycle.
- **Depends on:** MVP live-preview pipeline (crop+scale, see
  `reference/process-assumptions-audit.md`).

#### T14 — Pickup-failure error dialog after 3 consecutive failures — base behavior confirmed (ijon, 2026-07-26), UI resolution added (ijon, 2026-07-29)
- **Scope:** carry over `scan-process.md` step 8.2 — after 3 consecutive
  pickup failures, instead of retrying indefinitely **or silently
  halting**, present the user an explicit choice on the touchscreen:
  1. **"Stop job"** — ends Auto-Scan, same as pressing Stop; leaves the
     machine in the stopped state for T9/T10 recovery.
  2. **"Try again"** — resets the failure counter and allows 3 further
     attempts on the same page slot (re-enters T23/T24's normal retry
     loop, not a single extra try).
  3. **"Manual pageturn"** (new) — user physically turns the page by
     hand, then confirms via a touchscreen button press; Auto-Scan then
     continues automatically.
- **Open design question, not resolved here:** how "Manual pageturn"
  interacts with T24's held attempt-1 photo for the failed page slot.
  T23 always captures the photo *before* the pickup/turn check, so
  attempt 1's already-held photo is a valid scan of the current
  (pre-turn) page regardless of whether the automated turn itself
  succeeded — archiving that held photo under this slot's
  `sequence_number` once "Manual pageturn" is confirmed (then starting a
  fresh slot for whatever page comes next) seems the sensible behavior,
  rather than discarding it the way the current 3-failure-abort design
  does. **Not confirmed with ijon — flag for decision before T24 is
  updated to match.**
- **Done when:** triggering 3 consecutive pickup failures during a test
  Auto-Scan run shows this choice dialog, not a bare halt; "Stop job"
  leaves the machine stopped exactly as pressing Stop does; "Try again"
  grants 3 further attempts on the same page before the dialog can
  reappear; "Manual pageturn" followed by physically turning the page
  and confirming resumes Auto-Scan automatically.
- **Depends on:** MVP Auto-Scan loop, pickup-failure detection (already
  MVP), T24 (retry/page-slot bookkeeping — needs updating to support
  "Try again" and "Manual pageturn," see open question above).

---

## AD-005: Metadata Capture Flow (Component 6)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§6. All open design questions for this component are resolved
(2026-07-26) — camera geometry, offline handling (out of scope for
MVP), and the step-5 UI pattern (search-after-typing, confirmed).

#### T15 — Cover photo capture (front + back)
- **Scope:** HAL moves the box to the top (reusing the same primitive
  as T9); UI Shell prompts the user to place the **closed** book on
  the right side (front cover) → Camera Capture takes a single shot
  (right camera) → prompts the user to move the book to the left side
  (back cover) → single shot (left camera). Both images are stored as
  the book's cover photos, available to later steps and as the Z3
  thumbnail source.
- **Done when:** running this flow on a test book produces two saved
  images (front, back) associated with the job, each manually verified
  to show the correct respective cover, without requiring any capture
  geometry beyond what the existing left/right cameras already provide.
- **Depends on:** HAL move-to-top primitive (T9), Camera Capture
  Service's single-shot capture (not yet its own task — see
  `reference/z1-components.md` §2 "Not yet broken into tasks").
- **MVP scope stays exactly this — raw photos, used as-is.** A later
  version needs to rectify these (corner detection, perspective
  correction, crop, color-correct) the same way normal pages eventually
  get dewarped, but with different needs (closed/rigid book, no fixed
  reference rectangle) — not MVP work, see T33 /
  [`reference/cover-photo-postprocessing.md`](reference/cover-photo-postprocessing.md).

#### T16 — Public Book Database Lookup (shared sub-capability)
- **Scope:** a lookup capability with two query modes — exact ISBN
  lookup, and title/author search — against Open Library (no key
  needed) with Google Books as secondary (key required). Returns a
  structured metadata candidate (title, author, publisher, year, cover
  URL if available) or no-match. Shared by T17, T18, and T19 — built
  once, not reimplemented per caller.
- **Done when:** given a known test book's ISBN, exact-lookup mode
  returns correct structured metadata; given a partial title/author
  string for the same book, search mode returns at least one relevant
  candidate.
- **Depends on:** network connectivity (MVP assumes it's present, per
  2026-07-26 decision — no offline handling needed).

#### T17 — ISBN acquisition sub-flow
- **Scope:** prompt the user to check for an ISBN barcode anywhere it
  might be (cover **or** first/last interior pages, not cover-only —
  corrected 2026-07-26). Three branches: **(2a)** barcode found — cover
  case reuses T15's already-captured photos directly (no repositioning
  needed), interior-page case opens the book to the relevant page in
  the normal cradle position and takes a fresh shot; either way, decode
  the barcode via a **barcode-decoding library** (e.g. zbar/zxing-style
  — a distinct technique from Component 3's tesseract text-OCR, not the
  same capability, worth keeping separate). **(2b)** ISBN printed but
  no scannable barcode — user types the digits via on-screen keyboard.
  **(2c)** no ISBN found at all — skip to T18. Branches 2a/2b feed the
  resulting ISBN into T16's exact-lookup mode; a hit ends the flow here
  (already DB-authoritative, no further verification needed); a miss
  falls through to T18.
- **Done when:** for a test book with a visible cover barcode, the flow
  decodes and looks it up **without** asking the user to reposition the
  book (reusing T15's photo); for a test book with only a printed ISBN,
  typing it manually produces a correct lookup via T16; for a test book
  with no ISBN, the flow falls through to T18 without getting stuck.
- **Depends on:** T15 (cover photos, cover-barcode case), T16 (ISBN
  lookup mode), a barcode-decoding capability (new, not yet covered by
  any existing component in AD-004 — Component 3 is text-OCR only).

#### T18 — Cover-LLM extraction & search fallback
- **Scope:** when T17 doesn't produce a confirmed result, send T15's
  cover photos to an image-recognizing LLM (local AI hardware,
  Minisforum MS-S1 Max) to extract a candidate title/author, then run
  that candidate through T16's search mode. Present the result to the
  user for confirmation on the touchscreen — never auto-accepted, since
  it's a guess, unlike T17's direct ISBN hit.
- **Done when:** for a test book without a usable ISBN, running its
  T15 cover photos through this path produces a candidate suggestion
  the user can accept or reject; accepting it produces the same
  finished-metadata shape as T17's successful path.
- **Depends on:** T15, T16, a reachable path from Z1 to the local AI
  hardware's LLM inference endpoint (not yet specified — same general
  open question as how Z1 reaches any VM-side service, not specific to
  this task).

#### T19 — Manual fallback form + database verification
- **Scope:** on-screen-keyboard form for title/author (and other basic
  fields) as the final fallback when T17 and T18 both fail. Confirmed
  UI pattern (2026-07-26): **after** the user finishes typing, search
  via T16's mode and show candidate matches for the user to confirm or
  dismiss — not live as-you-type autocomplete.
- **Done when:** typing a known test book's title and confirming a
  suggested match produces the same finished-metadata shape as T17/T18;
  typing a title with no DB match and dismissing all suggestions still
  produces a valid, minimal metadata record from the typed values
  alone.
- **Depends on:** T16 (search mode).

---

## HAL — Core Waypoint & Sensor Primitives (Component 1)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§1. Wiring topology resolved (ijon, 2026-07-26): MKS-ESP32-FOC board
direct to RPi (not via Arduino) — corrects an earlier, premature
"already decided" claim in AD-001. PM-side note: this decision still
needs recording in `project-management/backlog.md`/`todo.md` (P1b), not
done here.

#### T20 — FOC-board serial protocol client + RPi-side waypoint driving
- **Scope (revised 2026-07-29 against the firmware spec linked below —
  several assumptions in the previous version of this task are now
  stale, corrected here):** RPi-side client for the ESP32 board's serial
  protocol (built on SimpleFOC's `Commander`, extended). Board is wired
  **direct to the RPi via USB**, not via the Arduino. Covers:
  - **Touchdown** — not a plain "move to an absolute position." The
    pickup/press position ("Zero" in the older scan-process numbering)
    is **not a fixed coordinate** — it's found per-attempt by the
    firmware's closed-loop descend-until-contact sequence, since it
    depends on book thickness/binding. T20 must expose a `touchdown()`
    operation (not `move_to(ZERO)`), returning once the firmware
    reports contact detected, including the stop position and the
    compression/press value reached (needed for T12's pressure log, see
    T23).
  - **Hold-after-touchdown, then resume on command (corrected 2026-07-29
    — an earlier draft of this task wrongly described a "lift to scan
    position" step; there is no such thing, see below):** the scan
    photos are taken **while the box is down, compressed at the
    configured press value, and completely stationary** — the firmware
    holds there and does not move at all, and does not proceed on its
    own. T20 must send a further explicit "continue" command **only
    after** `capture_pair()` (T25) has actually succeeded — **not just
    naturally fall through in sequence**, this handshake is
    firmware-enforced; the firmware refuses to move without it.
  - **Homing** — a *distinct* operation from move-to-top (T9), runs once
    at machine startup, only after an explicit user confirmation shown
    by the UI Shell (not automatically on power-on — the firmware
    refuses to move without it). Establishes the axis's absolute zero;
    until it succeeds, the firmware's "homed" state is false and it
    **refuses** every other absolute-position command (move-to-top,
    touchdown-adjacent lifts, percentage-of-page-width moves) with an
    error. T20 must surface that "not homed" error distinctly, not as a
    generic failure — the UI needs to tell the user "home the machine
    first."
  - **Move to a position expressed as a percentage of the calibrated
    page width** (`scan-process.md` step 8: 30%, 60–70%, −10% relative,
    80%) — unaffected by the above, plain absolute/relative moves once
    homed.
  - **Two distinct abort/stop operations, not one — corrected 2026-08-02
    (was originally scoped as a single undifferentiated "immediate
    stop/abort," the same category-blur `ligature.md` itself
    originally had and has since fixed, §3.8):**
    - `abort_attempt()` (`M53`, `ligature.md` §6.2) — the **routine**
      operation the pickup-success check (T23) uses to cancel an
      in-flight `complete_page_turn()` on a failed pickup. Stops motion
      only; stays armed and homed; the caller sends a fresh
      `touchdown()` right after, no re-arming needed.
    - `stop()` (`M112`) — **genuine emergency stop only**, for the Stop
      button (T24) and unsolicited firmware faults (below). Disarms and
      puts the board into a latched `Fault` state — T20 must also expose
      `clear_fault()` (`M999`), which clears the latch but does **not**
      itself re-arm or re-home; the caller still needs a fresh `arm()`
      (and `home()`) afterward. Never use `stop()` for the routine
      pickup-failure case — that's what `abort_attempt()` is for.
  - **Unsolicited error handling** — the firmware can send an error
    without being asked (in particular: an unexpected-endstop hard-stop,
    cutting power immediately with no deceleration, and also latching
    `Fault` per the point above). T20 must listen for and surface these,
    not just handle request/response pairs — treat the same as a
    Stop-button press in whatever's driving the loop (T24), including
    the fact that recovery now requires `clear_fault()`+`arm()`, not
    just resending whatever move was interrupted.
  - **Telemetry beyond position/homed** — `status()` (`?`) now also
    reports velocity, torque/force, and drive mode, and its state enum
    includes `Probing` (a `touchdown()`'s active contact-search phase)
    as distinct from a plain `Moving` positional move — T20 should
    surface this distinction, not collapse it. A continuous unsolicited
    `status` heartbeat also exists (`ligature.md` §4.2/§12,
    rate-configurable via `M155`) — primarily useful for the diagnostic
    tool (`foc_diag`) and touchdown-tuning work, not required for T20's
    own normal operation, which gets what it needs from polling
    `status()` directly.
  - **mm/force calibration trigger** — the position (mm-per-motor-
    revolution) and press-force calibration procedures run as firmware
    on the ESP32 itself, but must be triggerable from the RPi side (they
    are explicit, user-triggered motions, never automatic). **No UI
    screen for this exists yet** in T30's happy-path flow — needs a new
    screen/entry point, not scoped further here.
  - Report movement completion (including touchdown's stop
    position/compression value) back to the RPi for every operation
    above.
- **Resolved (ijon, 2026-07-29) — was flagged as an open discrepancy in
  an earlier draft of this task, no longer open:** the page photo is
  taken **at** the touchdown/press position itself, box fully
  stationary, matching T23/T27's existing design — there is no separate
  "scan position" the box lifts to first. T23's existing step ordering
  (touchdown → photo → *then* fan/vacuum) already has this right; what
  was missing (now added above) is the explicit firmware-enforced
  "don't move until told to" handshake around the photo step.
- **Done when:** on the real machine, a "move to 50% of a 148mm page
  width" test command moves the physical box to the corresponding
  position and reports completion; a `touchdown()` command reliably
  stops on contact and reports stop position + compression value; a
  homing command establishes zero, after which absolute moves succeed —
  the same absolute moves attempted *before* homing are refused with a
  distinguishable error; an unexpected-endstop fault sent by the
  firmware is received, latches `Fault`, and is treated as equivalent to
  a Stop — **and** (added 2026-08-02) `abort_attempt()`/`M53` correctly
  cancels an in-flight `complete_page_turn()` without disarming or
  faulting, verifiably distinct from what `stop()`/`M112` does.
- **Depends on:** FOC board wired direct to RPi, SimpleFOC firmware
  flashed and configured per `ligature.md` (outstanding hardware/
  firmware bring-up, tracked separately — protocol client code can
  proceed in parallel, but end-to-end verification needs that firmware
  work done first; current bring-up status/sequence:
  `ligature-AGENTS.md`).
- **Full requirements document:** [`ligature.md`](ligature.md) —
  self-contained, authoritative protocol spec this task's client code
  must match (full command set, safety requirements incl. the `M112`/
  `M53`/`Fault` distinction §3.8, telemetry §12, worked example §14).
  **Corrected 2026-08-02:** this used to point to
  [`reference/esp32-foc-firmware-requirements.md`](reference/esp32-foc-firmware-requirements.md)
  as the primary spec — that document predates `ligature.md` and was
  itself superseded by it for protocol matters back at AD-006
  (`architecture.md`); it remains useful for hardware-only detail
  (GPIO map, connector pinouts, open hardware-verification items) but
  isn't what this task's client code should be checked against anymore.
  Milestone 1 (safe demo/jog, no real page-turning, per the reference
  doc) is a narrower first target than this task's full scope —
  Milestone 2 corresponds to T32 (firmware side) and this task's full
  behavior (RPi side).

#### T21 — BMP180 pressure read (Arduino-side firmware + RPi client) — firmware+client slice done (2026-08-01/02)
- **Scope (revised 2026-08-01 — AD-001's RPi-direct BMP180 decision was
  reversed; the sensor stays on the Arduino, see `architecture.md`
  AD-001 and `monospace.md` §1/§5/§6):** ~~RPi-side I2C driver (e.g. via
  `smbus2`) reading the BMP180 pressure sensor directly~~. Now two
  halves, like T20/T22: **(a) Arduino-side** — implement `PRESS?`
  (single-shot, averaged) and `PRESS START`/`PRESS STOP` (continuous
  streaming) per `monospace.md` §5/§6; `monospace`'s existing
  `SFE_BMP180` driver code is a direct reference for the Bosch
  calibration/read procedure here (unlike the old RPi-side-rewrite plan,
  this C++ code is now actually reused, not just referenced for its
  formulas). **(b) RPi-side** — the Rust Arduino client's pressure
  functions (`sans-serif.md` §1.2): `read_ambient_pressure()`,
  `start_pressure_stream()`/`stop_pressure_stream()`. Consumed by the
  Auto-Scan Controller (T11/T12/T14) and the Setup-Calibration
  Controller (page-width derivation, T3).
- **Done when:** (a) `PRESS?` on the physically connected Arduino
  produces a plausible ambient-pressure value, sanity-checked against a
  known reference (e.g. a phone barometer app or published local
  atmospheric pressure); (b) `PRESS START` sustains a measured reading
  rate against the target in `monospace.md` §6 (≥5/s, ideally up to
  50/s) without corrupting relay-command responses sent on the same
  connection while streaming.
- **Depends on:** BMP180 wired to the Arduino (confirmed, 2026-07-25);
  Arduino firmware implementing `PRESS?`/`PRESS START`/`PRESS STOP` —
  **done, see below**, no longer a blocker.
- **Pulled forward (ijon, 2026-08-02, see `architecture.md` AD-007 for
  the full reuse/alternatives reasoning):** the RPi-side pressure-read
  client's first concrete deliverable is a diagnostic binary at
  `sans-core/src/bin/hw_diag.rs` (shared with T22's relay commands,
  same binary, same naming convention as the existing `camcal.rs`),
  rather than waiting for full Auto-Scan integration.
  - **Subcommands:** `press` (single-shot, prints one `mbar` value) and
    `press-stream` (prints timestamped values as they arrive, plus an
    optional `--log <file.csv>` flag — needed because deriving
    real pickup-success/-failure thresholds from this data requires
    something more parseable than terminal scrollback).
  - **Usage (SSH, once built):**
    `./target/release/hw_diag --port /dev/ttyACM0 press` and
    `./target/release/hw_diag --port /dev/ttyACM0 press-stream --log pressure-test.csv`.
  - **Zero-code fallback while this binary doesn't exist yet:**
    `arduino-cli monitor -p <port> -c baudrate=<rate>` (`monospace.md`
    §8) already lets you type `PRESS?`/`PRESS START` and read responses
    over SSH — use it for the first "does the board even respond" check
    while `hw_diag` is still being built.
  - **Verification order:** confirm each pressure command works via
    `arduino-cli monitor` first, *then* re-verify the same command
    through `hw_diag` once it exists — the CLI's own serial handling is
    new code and needs its own check, not just a port of
    already-verified firmware behavior.
  - Once working, it's expected (not just tolerated) that ijon uses
    `press-stream --log` during manual pickup attempts on a real book
    to start collecting threshold-relevant data — that's the actual
    point of pulling this forward rather than waiting for full
    Auto-Scan integration.
  - **Done (2026-08-01/02):** `monospace` PR #2 → `master` (`17d547b`)
    implements `PRESS?`/`PRESS START`/`PRESS STOP` exactly per
    `monospace.md` §5/§6, including the AVR watchdog (`WDTO_8S`) and
    `\r\n`-tolerant line parsing from §3/§4. `sans` PR #6 → `master`
    (`beab768`) adds `hw_diag` as the single-session interactive tool
    from `monospace.md` §9, plus the typed Rust client functions
    (`sans-core/src/hardware/`). **Both "Done when" criteria above are
    met on real hardware:** (a) `PRESS?` produces plausible ambient
    values (~1016 mbar, matching normal atmospheric pressure); (b)
    `PRESS START` sustained ~49 Hz at oversampling=2 — comfortably above
    the ≥5/s target and close to the ≤50/s ideal, with relay commands
    still working correctly interleaved with streaming (confirmed via
    the manual multi-attempt test, `docs/hardware/bmp180-vacuum-drop-test.md`).
    Chosen baud rate: `115200` (not the old firmware's `9600` or old
    Rust client's `9200`, per the explicit warning in `monospace.md`
    §8/testing notes). **Not yet done, separate from this task:**
    consuming these client functions from the actual Auto-Scan
    Controller (T24) or Setup-Calibration Controller (T3) — this task's
    scope was the firmware + a standalone diagnostic client, not
    application integration.

#### T22 — Arduino relay protocol design + RPi-side client — firmware+client slice done (2026-08-01/02)
- **Scope (revised 2026-07-29 — walking back the previous "integration,
  not new development" framing after actually reading `sans-core` and
  `monospace` in full, see
  [`reference/sans-code-reusability-review.md`](reference/sans-code-reusability-review.md)):**
  SW0 (RPi-app language) is **Rust**, decided as a consequence of reusing
  `sans-core::hardware` — that part stands. **What doesn't stand:** the
  claim that `monospace` + `sans-core::hardware` already provide the
  granular commands this task needs. They don't. The real, currently
  compatible protocol between them has exactly three coarse commands
  (`MoveBox`, `Lighting` on/off only, and a single monolithic
  `FlipPage(spine_width)` that runs an entire flip cycle autonomously on
  the Arduino with no mid-sequence feedback to the host) — confirmed by
  reading `bookscanner.cpp`'s `flip_page()` directly. There is no
  existing `vacuum_on()`/`fan_on()`/`turn_blower_on()`/`light_auto()` at
  the protocol level, and `flip_page()` can't be reused even reworked —
  it's inseparable from direct stepper-motor control that no longer
  exists (moved to the ESP32, P1).
  **This task is therefore real protocol design, not integration:**
  design a new Arduino-side command set exposing vacuum on/off,
  page-separation fan on/off, turn-blower on/off, and light on/off/auto
  as separate operations, plus a matching RPi-side Rust client. (BMP180
  pressure commands are T21's scope, not this task's — same board and
  protocol document, `monospace.md`, but tracked separately since they
  were originally a separate RPi-direct task before AD-001's reversal.)
  **Dropped (ijon, 2026-08-01): a pneumatic safety-sensor read is no
  longer part of this task.** That was scoped for a possible future
  rework replacing the page-separation fan/turn blower with compressed
  air + solenoid valves — not pursued for now; see `architecture.md`
  AD-001/AD-006 if it needs reconsidering later. Reusable facts to build
  on (confirmed by reading
  the code, not assumed): the relay pin assignments and **active-low**
  polarity in `bookscanner.cpp` (`set_fan`/`set_vac_pump`/
  `set_blow_pump`/`set_lights` all invert the state), and
  `sans-core::hardware`'s generic byte-stream framing
  (`Response::build`'s length-prefixed payload parser, the serial-port
  read/write loop in `hardware/mod.rs`) — the low-level plumbing, not the
  command vocabulary. **The endstop is no longer read via the Arduino** —
  it moved to a GPIO on the ESP32 FOC board itself (see T20/T32), so
  homing and move-to-top's endstop-proximity behavior are entirely T20's
  concern now, not this task's. **Also needs an immediate
  all-actuators-off operation** (vacuum, fan, blower off at once) for
  Stop-button handling, exposed as a single callable operation, not three
  separate calls that could partially fail.
- **Done when:** sending each new command type to the physically
  connected Arduino (running the newly designed firmware) produces the
  expected physical effect (vacuum engages, fan spins, blower engages,
  light toggles/auto-mode works).
- **Depends on:** Arduino wired and running (confirmed in place since
  2026-07-24/25, `docs/hardware/electronics.md`); new Arduino firmware
  design/implementation — **done, see below**, no longer a blocker.
- **Pulled forward (ijon, 2026-08-02, see `architecture.md` AD-007 for
  the full reuse/alternatives reasoning):** the same `hw_diag` binary as
  T21 above also gets the relay commands as its first deliverable,
  ahead of full Stop-button/Auto-Scan integration.
  - **Subcommands:** `vacuum on|off`, `fan on|off`, `blower on|off`,
    `light on|off`, `all-off` — one-to-one with `monospace.md` §5, no
    invented behavior beyond it.
  - **Usage (SSH, once built):**
    `./target/release/hw_diag --port /dev/ttyACM0 vacuum on` — this
    physically energizes the pump exactly as typing `VACUUM ON` into a
    raw serial terminal would; same physical-safety handling applies
    (be present/aware before running it, don't script unattended
    repeated actuator-on loops).
  - **Branch/PR recommendation (not mandated):** this is a full wire-
    protocol rewrite on the `monospace` side (binary → text-line, see
    T22's scope above) plus a new binary on the `sans` side — prefer a
    feature branch in each repo, PR against `master` once the new
    protocol/CLI works end-to-end against real hardware, rather than
    landing an in-progress rewrite directly on `master`. Direct-to-
    `master` is ijon's call to make explicitly if preferred instead.
  - **Not in scope for this pulled-forward slice:** wiring `hw_diag`'s
    client code into the Auto-Scan Controller or Stop-button handling
    (later work, full `sans-serif.md` integration), or turning derived
    pressure thresholds into actual pickup-success/-failure decision
    logic (T23). This slice produces the tool and the measurement data
    only.
  - **Done (2026-08-01/02):** landed via feature branches as
    recommended above — `monospace` PR #2 → `master` (`17d547b`), `sans`
    PR #6 → `master` (`beab768`). **"Done when" above is met:** all four
    relay commands (`VACUUM`/`FAN`/`BLOWER`/`LIGHT` on/off, plus
    `ALL OFF`) work against the physically connected Arduino via
    `hw_diag`, and the relay→physical-actuator mapping was independently
    verified and confirmed correct by ijon (2026-08-02) — matching
    `monospace.md` §2's pin table (`VAC_PUMP`=D4, `PES_PUMP`=D5,
    `LAMP`=D6, `FAN`=D7) exactly, no surprises. `LIGHT AUTO` remains
    unimplemented, as specified (excluded from MVP, `monospace.md` §5).
    **Not yet done, separate from this task:** Stop-button handling and
    Auto-Scan integration (T24) — still application-level work this
    slice deliberately didn't cover.

---

## Auto-Scan Flip-Cycle Controller — Core Loop (Component 5)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§5. Split into two tasks — the physical per-cycle mechanics (T23) and
the control-flow/recording loop around it (T24) — so each is
independently testable: T23 against real hardware behavior, T24
against call sequencing and data handling (which can be verified with
T23 stubbed/mocked). Resolves the long-open item 8 in
`reference/z1-z3-triage-overview.md` ("FOC-Motoransteuerung vs.
Prozent-Wegpunkte") — T20 already covers percentage-of-page-width
translation, so this is no longer an unconfirmed assumption.

#### T23 — Single pickup attempt

> Rewritten twice on 2026-07-26. First pass was too thin (keyword-teased,
> not a concrete sequence) — fixed by spelling out every step against
> `scan-process.md`'s own numbering. Second pass: ijon caught a real
> design flaw in how T23/T24 handled pickup failure (see T24's changelog
> note below for the full reasoning) — **T23's unit of work changed from
> "one full flip-cycle" to "one pickup attempt"**, because a failed
> pickup should retry locally (back down to Zero, try again) rather than
> discard everything and restart the whole cycle from Up-Position.

- **Inputs:** `page_width_mm` (from Setup-Calibration, §4, passed down
  through T24). T23 does not know or care whether it's being called as
  the 1st, 2nd, or 3rd attempt for the current page — that bookkeeping
  is entirely T24's job (see below). Every call to T23 is identical.
- **Safety invariant (ijon, 2026-07-26 — binding on every step below,
  not just the failure branch):** the suction box must **never** move
  downward while the vacuum motor is running, with exactly **one**
  exception — the page-separation-wiggle move at step 10 (8.11, the
  relative −10%-of-page-width move within the 50–70% zone). Every other
  downward move in this sequence must have vacuum **and both blow
  units** (separation fan, turn blower) switched off *first*. The
  sequence below already satisfies this (failure branch: fan+vacuum off
  → then move to Zero; attempt start: vacuum is off from the previous
  attempt's/slot's cleanup before the initial move to Zero) — called
  out explicitly here as a named invariant so a future edit to this
  sequence doesn't silently break it. **Recommendation, not yet
  decided:** T20 could defensively refuse a non-wiggle downward move
  while T22 reports vacuum engaged, as a second line of defense beyond
  "T23 happens to call things in the right order" — worth considering
  when T20/T22 are actually implemented, not decided here.
- **Scope — ordered sequence (one attempt):**
  1. **(8.1)** T21 `read_ambient_pressure()` → `p_baseline`. Must happen
     before any pneumatics for this attempt engage (steps 5/7 below) —
     read fresh on every attempt, not reused from an earlier one, since
     it's meant to capture the true no-vacuum baseline immediately
     before *this* attempt's suction test.
  2. **(8.3)** T20 `touchdown()` — **corrected 2026-07-29 (was
     `move_to(ZERO)`):** "Zero" is not a fixed coordinate — it's found
     per-attempt by the firmware's closed-loop descend-until-contact
     sequence, since it depends on book thickness/binding, which can
     differ page to page. Every attempt (including retries) re-runs a
     real touchdown, not a move to a remembered position. Record the
     returned stop position and compression/press value — needed for
     T12's pressure-log entry (log alongside `ambient_mbar`/
     `differential_mbar`, not a separate mechanism).
  3. **(8.4)** If the light-switch is in Auto mode: T22 `light_on()`.
  4. **(8.5)** Camera Capture `capture_pair()` — both cameras
     simultaneously, box fully stationary at the touchdown/compressed
     position (T20's firmware holds it there and will not move until
     told to — see T20). **Taken on every attempt, not just the first**
     (ijon, 2026-07-26): even though only the first attempt's photo
     ends up archived (T24 decides that), every attempt's photo gets
     OCR'd (T2, via the Page-Number Recognition Service, §3) so its recognized page number
     can be cross-checked against the first attempt's — confirming
     we're still looking at the same page, which is more reliable than
     just assuming nothing changed between attempts. Hold the two raw
     images (`img_left`, `img_right`) in the returned result; T23 itself
     does not persist them. **New required step (2026-07-29): once
     `capture_pair()` succeeds, T20 must be sent the explicit
     "proceed"/continue command before any of the following steps run**
     — the firmware is holding and will refuse to move otherwise (see
     T20's hold-after-touchdown handshake).
  5. **(8.6)** T22 `separation_fan_on()`.
  6. **(8.7)** If the light-switch is in Auto mode: T22 `light_off()`.
  7. **(8.8)** T22 `vacuum_on()`.
  8. **(8.9)** T20 `move_to(30% of page_width_mm)`. T21
     `read_differential_pressure()` at this position → `p_30`. Compare
     `p_30` to `p_baseline` against the pickup-success threshold
     (currently untuned — see T12's `threshold_mbar_used` logging,
     built for exactly this reason):
     - **Failure branch** (pressure didn't drop enough — page wasn't
       picked up): T22 `separation_fan_off()`, T22 `vacuum_off()` —
       **the original process text does not explicitly say to do this
       on the failure branch; proposed here as the sensible cleanup so
       the attempt doesn't leave pneumatics running, not a verbatim
       requirement from the source.** Then a fresh T20 `touchdown()` for
       the retry — **corrected 2026-07-26 (was `move_to(UP_POSITION)`): a
       failed pickup retries locally, it doesn't abort the whole cycle**;
       **corrected again 2026-07-29 (was `move_to(ZERO)`): re-touchdown,
       not a move to a remembered position, per step 2's correction
       above.** Return
       `{pickup_result: "failure", images: {img_left, img_right},
       ambient_mbar: p_baseline, differential_mbar: p_baseline - p_30}`
       — **images are returned even on failure now** (T24 needs them
       for the page-number cross-check, and needs to keep them on hand
       in case this was attempt 1 — see T24). **T23 ends here on
       failure** — steps 9–13 below do not run.
     - **Success branch:** continue to step 9.
  9. **(8.10)** T20 `move_to(60–70% of page_width_mm)` — the original
     text gives a range, not a single value; the exact target within
     that range is an implementation/tuning choice, not narrowed
     further here. T22 `separation_fan_off()`.
  10. **(8.11)** T20 `move_relative(-10% of page_width_mm)` — a further
      downward move by 10%-of-page-width, relative to wherever step 9
      left the box (not an absolute position).
  11. **(8.12)** T20 `move_to(80% of page_width_mm)`, then T22
      `turn_blower_on()`. **Simplification flagged:** "switches on
      Page-Turn-Blower at 80% of page width" in the original could mean
      the blower engages exactly at the moment the box crosses 80%
      mid-move, not only after arrival. Move-then-engage is the simpler
      reading, adopted here as a starting point — needs validating
      against actual page-turn behavior on the real machine; the blower
      may need to fire earlier/concurrently for a reliable turn.
  12. **(8.13, corrected 2026-07-29 — previously said the box goes all
      the way to the T9 move-to-top position; wrong, and not this
      primitive at all):** continue upward to **110–120% of
      page_width_mm** — **provided that target is still below the actual
      physical top-of-travel; if it isn't, fall back to a safe
      top-adjacent position instead** (exact fallback threshold not
      specified further here, needs on-hardware validation). This is far
      enough to ensure the page has fully turned over, without needlessly
      traveling all the way to the top or touching the endstop. Then
      **reverse direction and move back down** — heading toward the next
      page's touchdown, not resting at the top. T22 `turn_blower()` stays
      **on** through the reversal and initial descent; **turn it off only
      once the box passes back down through 90% of page_width_mm**, not
      at the top of the up-move and not immediately on reversal. **Vacuum
      cut-off timing not specified by ijon — flagged as open, not
      assumed:** natural page separation likely happens near 100% of
      page_width_mm on the way up (the same physical event T27's
      page-width calibration measures), but whether `vacuum_off()` should
      tie to that detection, to the 110–120% peak, or to some other point
      isn't decided here.
  13. Return `{pickup_result: "success", images: {img_left, img_right},
      ambient_mbar: p_baseline, differential_mbar: p_baseline - p_30,
      touchdown_position, touchdown_compression}` — the last two from
      step 2's `touchdown()` result, for T12's log. **The box is on its
      way back down** (per step 12, corrected 2026-07-29) — not resting
      at any top position — when this returns; the *next* page slot's
      `touchdown()` call (T23 step 2, invoked fresh by T24) is what
      actually brings it to a stop, no separate resting waypoint exists
      between page slots.
- **Done when:** on the real machine, triggering one attempt with a
  normal test page executes steps 1–13 in this exact order, produces two
  raw images, reaches 110–120% of page_width_mm (or the safe top-adjacent
  fallback) without touching the endstop, and ends with fan and blower
  off (blower specifically after crossing back down through 90% of
  page_width_mm) while descending back toward the next touchdown;
  deliberately jamming/weighting a test page triggers the failure branch
  at step 8 (fan/vacuum off, box back at a fresh touchdown,
  `pickup_result: "failure"`, images still returned) instead of
  proceeding through steps 9–13 as if the page had turned; calling T23
  again immediately after a failure starts a fresh attempt (a new
  `touchdown()` call) without any detour to the top.
- **Depends on:** T20, T21, T22 (HAL primitives), Camera Capture
  Service's capture-pair operation (not yet its own task, see §2's
  "Not yet broken into tasks").

#### T24 — Auto-Scan loop driver (control flow, recording, Stop handling)

> **Changelog (ijon, 2026-07-26):** the original version of T23/T24
> discarded a failed cycle's photo entirely and restarted the whole
> cycle from Up-Position on retry — ijon flagged this as nonsensical:
> the photo already taken is still a valid photo of the (still
> unturned) page, and the retry doesn't need to revisit Up-Position at
> all, just go back down to Zero and try the pickup again. Fixed below:
> retries are now scoped inside a single page slot (T23 called multiple
> times), the first attempt's photo is what gets archived regardless of
> which attempt's pickup actually succeeds, and every attempt still gets
> OCR'd for a page-number cross-check even when its photo isn't archived.

- **Scope:** for each **page slot** (identified by the `sequence_number`
  it will occupy once archived):
  1. Assign the page slot's `sequence_number` **now, at the start**
     (T1, corrected 2026-07-26 — reverses the same-day-earlier
     "assigned only on pickup success" refinement, which was based on
     the now-corrected discard-on-failure design). Left camera →
     even number, right camera → odd number, as before.
  2. Call T23 (attempt 1). **Keep this attempt's `images` result in
     hand regardless of outcome** — per ijon's confirmed choice
     (2026-07-26), this is the photo that ultimately gets archived
     under this page slot's `sequence_number`, no matter which attempt
     succeeds.
  3. Run the Page-Number Recognition Service (§3, not yet its own task) on this attempt's
     images → `recognized_page_number` per side. Emit this attempt's
     `ambient_mbar`/`differential_mbar` for live display (T11) and
     logging (T12) — every attempt logs its pressure values, not just
     the first.
  4. **If `pickup_result: "success"`:** persist attempt 1's images
     (held since step 2) under this page slot's `sequence_number` (T2:
     store attempt 1's `recognized_page_number`, not necessarily this
     attempt's, if they differ — attempt 1's OCR reading is the one
     that describes the archived photo). Reset the pickup-failure
     counter (T14). Advance to the next page slot (back to step 1).
  5. **If `pickup_result: "failure"`:** a mismatch between this
     attempt's `recognized_page_number` and attempt 1's **resolved**
     (ijon, 2026-07-26) — not a real anomaly needing special handling.
     Physically, a pickup that fails at the 30% check (T23) never
     reaches the page-turn steps, so the page **cannot** have actually
     turned between attempts; any mismatch is therefore OCR misreading
     one of the two photos, not a real page change. No comparison logic
     needed beyond what's already the design: attempt 1's reading stays
     the one recorded for the archived photo regardless. Increment the
     pickup-failure counter; if it reaches 3, abort (T14) — the box goes
     to the actual T9 move-to-top position (distinct from a normal
     successful cycle's 110–120%-then-reverse motion, corrected
     2026-07-29 — see T23 step 12) only as part of this abort, not as
     part of the per-attempt failure handling itself (that stays at a
     fresh touchdown, per T23).
     **Discard this attempt's images** (they were only needed for the
     cross-check) **unless this was attempt 1**, whose images stay held
     per step 2. If under the limit, call T23 again for the same page
     slot (back to step 2, same `sequence_number`, no new one assigned).
- **Handles the Stop signal:** must interrupt promptly, including
  mid-attempt (not only between completed attempts or completed page
  slots) — requires T20's and T22's immediate-stop/all-actuators-off
  operations (added 2026-07-26, see above). **Also treats an unsolicited
  hard-stop error from T20 (unexpected-endstop fault, see T20/T32) the
  same as a user-pressed Stop** (new, 2026-07-29) — this is not something
  the loop requests, it can arrive at any time and must interrupt the
  current attempt exactly like Stop does. On Stop, T20's fault, or T14's
  abort, halts and leaves the machine in the stopped state for T9/T10
  recovery.
  A page slot that was mid-attempt when stopped has **no** archived
  image (attempt 1's held photo is discarded along with the job's
  in-progress state) — resuming after a Stop starts that page slot over
  from a fresh attempt 1, not from wherever the interrupted attempt was.
- **Done when:** running a multi-page test sequence produces correctly
  numbered, correctly OCR'd page records, one per page slot, using each
  slot's first-attempt photo even when a later attempt was the one that
  actually succeeded; an intentionally jammed test page produces one or
  more discarded (non-archived) retry attempts within the same page
  slot, all sharing that slot's one `sequence_number`, with no gap and
  no duplicate archived image; pressing Stop mid-attempt halts all HAL
  actuators immediately, without waiting for the current attempt's
  remaining waypoints to complete, and leaves that page slot
  unarchived.
- **Depends on:** T23 (single-attempt mechanics), T1/T2/T11/T12/T14
  (already-defined per-slot/per-attempt recording behaviors this task
  orchestrates, not redefines), Page-Number Recognition Service (§3, not yet its own task),
  T20/T22's stop/abort operations.

---

## Camera Capture & Preview Service (Component 2)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§2. Deliberately kept lean — capture and crop+scale are well-precedented
(both `alexandria` and `sans-core` have working single-shot capture),
not much novel design needed here. One real gap found and closed below
(rotation) rather than two padded-out tasks.

#### T25 — Camera capture-pair operation
- **Scope:** trigger both UVC cameras (left, right) via v4l2 for a
  synchronized shot, returning two raw images. **Rotation is applied
  here, not left to callers** (found 2026-07-26, missing from the
  component's original description despite being an already-established
  requirement — `reference/z1-z3-triage-overview.md` item 4: fixed
  camera mounting means correct rotation must happen on the RPi, "kein
  separater v2-Schritt, sondern Teil des MVP-Kamerapfads"). The exact
  rotation angle per camera (may differ between left/right if mounted
  as mirror images) is a fixed, hardware-measured constant, not computed
  at runtime. Minimal fault handling: if a camera fails to return a
  frame (disconnected, timeout, corrupt read), retry once, then surface
  a hard failure to the caller (T23) — this runs unattended across
  potentially hundreds of shots per book, a silent bad/missing image
  would be worse than a loud failure.
- **Done when:** on the real machine, calling capture-pair produces two
  correctly right-side-up images (visually confirmed, not sideways or
  upside-down); unplugging one camera mid-test produces a reported
  failure, not a silently blank/corrupt image.
- **Depends on:** physical camera mounting angle measured on the real
  machine (a hardware fact, not a software decision — needed to know
  the rotation constant per camera).

#### T26 — Preview crop+scale operation
- **Scope:** given a raw (already-rotated, per T25) image, a crop
  rectangle, and a target display size, produce a cropped+scaled image.
  The crop rectangle is always expressed in raw-image pixel space
  (post-rotation) — the same convention for all three callers
  (calibration preview, live full-page preview, live zoom preview,
  T13) — avoiding the coordinate-mapping bug already flagged in
  `reference/process-assumptions-audit.md` (tap coordinates need
  correct inversion between preview-space and raw-image-space).
- **Done when:** given a known crop rectangle and a raw test image, the
  output matches the expected cropped/scaled region (within normal
  resampling tolerance); calling it with a full-frame rectangle and a
  small target size reproduces the full-page preview variant, and with
  a small rectangle and no scaling reproduces the zoom variant —
  confirming one function serves all three callers without
  special-casing.
- **Depends on:** T25 (consumes its rotated output).

---

## Setup-Calibration Controller (Component 4)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§4 and [`reference/process-assumptions-audit.md`](reference/process-assumptions-audit.md)
("Combined setup-calibration flow"). Split the same way as the
Auto-Scan controller — mechanical/capture half (T27) vs.
interaction/mapping half (T28) — same rationale: independently testable,
T28 can be exercised against a fixed test image without needing the
real machine.

#### T27 — Calibration capture + page-width derivation
- **Note (resolved, ijon, 2026-07-26):** the two images this task
  captures are **ephemeral** — used by T28 to build the tap-preview and
  derive the crop rectangles, then discarded once T28 finishes. They
  are taken at a random, user-chosen page and have no place in the
  numbered page archive (unlike the Metadata Capture Flow's cover/back/
  ISBN-interior-page photos, §6, which are kept). No `sequence_number`
  is assigned to them.
- **Scope — ordered sequence:**
  1. UI Shell prompts the user to open the book to a page with visible
     page numbers and place it in the cradle; T27 waits for a
     "user ready" signal from the UI Shell (the prompt itself is §8's
     job, not this task's).
  2. T20 `touchdown()` — **corrected 2026-07-29 (was `move_to(ZERO)`,
     see T23's same correction):** lowers the box onto the page via the
     firmware's closed-loop descend-until-contact sequence, not a move
     to a fixed coordinate.
  3. T25 `capture_pair()` — the calibration shot, both cameras, box
     stationary at touchdown (no vacuum/fan yet). Hold `calib_img_left`,
     `calib_img_right`.
  4. Send T20 the explicit "proceed" command — the firmware is holding
     after touchdown (§T20's hold-after-touchdown handshake) and refuses
     further motion without it, same requirement as T23.
  5. T21 `read_ambient_pressure()` → `p_baseline`, **before** vacuum
     engages.
  6. T22 `separation_fan_on()`, T22 `vacuum_on()` — **corrected
     2026-07-29 (previous version said this move happens "without any of
     T22's fan/vacuum/blower operations" — wrong):** vacuum must
     actually be engaged here. The whole point of this step is detecting
     the pressure signature of a real pickup, which doesn't exist
     without suction.
  7. **Detection #1 — pickup-success check (drop), corrected 2026-07-29:
     this is the *same* check every normal page-turn already does
     (T23's pickup-success check), not a new algorithm invented for this
     task.** Shortly after vacuum engages, T21
     `read_differential_pressure()` should show a clear drop vs.
     `p_baseline`, confirming the page was actually picked up. If the
     drop doesn't happen, that's a pickup failure — same error condition
     as T23's failure branch (exact retry/abort handling for this
     calibration context not specified here).
  8. Once pickup is confirmed, T20 ascends — **corrected 2026-07-29:
     this is not the T9 move-to-top primitive** (established as a
     separate thing, see T9) — it's a distinct monitored-ascent
     operation that keeps moving and watching pressure until detection
     #2 below fires, not a move to a fixed target position. Purely
     vertical, **no page-separation-wiggle-move** (unlike T23's step
     8.11) — **this is a one-way measurement move, not a full flip
     cycle**: the box does not come back down afterward. While
     ascending, continue sampling T21 `read_differential_pressure()` —
     the value is expected to stay roughly steady here (page still held,
     pressure still low), not
     change continuously.
  9. **Detection #2 — page-separation rise, calibration-only (this is a
     second, distinct detection from #7 above — it does not run during a
     normal page-turn):** watch for the differential value **suddenly
     rising back** toward baseline, which happens once the page's
     trailing edge clears the suction box and it separates. **Exact
     detection algorithm not fully specified here, blocked on real
     hardware data** (see `process-assumptions-audit.md`: needs an
     actual measurement of the real signal shape). Placeholder for
     initial implementation: detect the differential value crossing back
     above a threshold, having been steady below it since step 7.
     **Possible offset needed (ijon, 2026-07-29):** the pressure sensor
     may have a reaction delay (a few ms between the physical separation
     and the sensor registering it) — a small compensating distance
     (roughly velocity × lag) may need to be added to the raw detected
     position; not fixed here, needs empirical determination.
  10. Record the box's position (from T20, converted to mm, plus any
      step-9 lag offset) at detection #2's point as `page_width_mm`.
      **Requires the ESP32's mm-per-motor-revolution calibration to
      already exist** (see T20's linked firmware requirements doc) —
      this task reads a calibrated mm position, it doesn't perform that
      calibration itself.
  11. T22 `vacuum_off()`, T22 `separation_fan_off()` — cleanup. Box ends
      up at the top; **the user manually pages the book back to its
      actual first page before Auto-Scan starts** (the calibration page
      was just some page chosen for a visible page number, not
      necessarily page 1 — this task does not return the book to any
      particular page).
  12. Return `{page_width_mm, calib_img_left, calib_img_right}`.
- **Done when:** on the real machine, running this against a test book
  of known page width produces a `page_width_mm` value reasonably close
  to the physically measured value (exact tolerance to be set once the
  real pressure-signal shape is known, per step 9) and produces two raw
  calibration images.
- **Depends on:** T20, T21, T22 (HAL — T22 added 2026-07-29, this task
  does engage vacuum/fan after all), T25 (Camera Capture), the ESP32-side
  mm-position calibration having been run (new dependency, 2026-07-29 —
  see T20). Step 9's detection algorithm can't be finalized/validated
  without measuring the real pressure signal on the machine — software
  structure doesn't block on this, final tuning does.

#### T28 — Tap-interaction calibration + coordinate mapping
- **Scope — ordered sequence:**
  1. T26 `make_preview()` on `calib_img_left`/`calib_img_right`, using
     the same fixed, pre-configured crop-then-scale rectangle as the
     live full-page preview (`process-assumptions-audit.md`: "the raw
     camera image likely needs a static-value crop first, then scaling
     down... not a direct scale of the full raw frame") — hand both
     previews to the UI Shell for display.
  2. UI Shell prompts "tap the page number on the left page" → reports
     `tap_point_left` back in **preview-display pixel space**.
  3. **Coordinate mapping** (the flagged gotcha, spelled out): convert
     `tap_point_left` from preview-space to raw-image-space using the
     same `crop_rect` from step 1:
     `raw_x = crop_rect.x + tap_point.x * (crop_rect.width / preview_display_width)`
     `raw_y = crop_rect.y + tap_point.y * (crop_rect.height / preview_display_height)`
  4. Center a **default-size rectangle** on `(raw_x, raw_y)` in
     raw-image space. Default size is a fixed constant, **needs
     empirical tuning on the real machine** (`process-assumptions-
     audit.md`: "needs to comfortably contain the digits regardless of
     tap precision... to be tuned empirically, not a research
     question") — not pinned to a specific pixel value here.
  5. If the user taps the same side again before confirming: **replace**
     the rectangle at the new point (one rectangle per side, not
     additive — already-decided behavior). Resizing the rectangle is
     explicitly deferred, not built here ("evtl. später").
  6. Repeat steps 2–5 for the right side.
  7. Return `{page_number_crop: {left: rect_left, right: rect_right}}`.
- **Done when:** tapping a known digit position on a test calibration
  image produces a rectangle that, fed through the Page-Number Recognition Service (§3, not
  yet its own task) against a similarly-positioned page image,
  successfully reads the printed number; re-tapping before confirming
  replaces rather than adds a second rectangle.
- **Depends on:** T27 (calibration images), T26 (preview crop+scale),
  UI Shell for tap-event reporting and prompt rendering (§8, not yet
  its own task).

**Resolved (ijon, 2026-07-26):** the two calibration photos are
**discarded, not archived** — they're taken at a random, user-chosen
page somewhere in the book, with no known position relative to the
actual page sequence, so they can't be slotted into the numbered
archive at all. No `sequence_number` (T1) is assigned to them. T27
still returns them (T28 needs the pixels to build the tap-preview and
derive the crop rectangles), but whoever completes the calibration flow
discards them once T28 finishes — they never reach the Job/Archive
Manager. This is a different bucket of images from the Metadata Capture
Flow's cover/back/ISBN-interior-page photos (§6, T15/T17) — **those**
are kept, for metadata extraction, and were never in question here.

---

## Page-Number Recognition Service (Component 3)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§3. Renamed from "OCR Service" (ijon, 2026-07-26) — the old name was
ambiguous with the unrelated, non-MVP full-page-text OCR that's part of
the eventual VM-side eBook pipeline (`target-state-requirements.md`:
"Rohbilder → Bildkorrektur → OCR → eBook", confirmed non-MVP in
`z1-z3-triage-overview.md` item 9). This component only ever recognizes
the small page-number crop, nothing else — kept lean, one task.

#### T29 — Page-number recognition operation
- **Scope:** given a raw image (post-rotation, from T25) and a
  calibrated crop rectangle (from T28), crop the region and run local
  `tesseract` on it, returning the recognized string or `null` if
  nothing legible. **Character set note:** front-matter pages may use
  roman numerals (i, ii, iii, iv...) rather than arabic digits — the
  `tesseract` call must **not** be restricted to a digit-only
  whitelist, or every roman-numeral page would read as `null`. No
  validation/sanity-filtering beyond basic cleanup (trim whitespace,
  empty string → `null`) — interpreting the result (arabic vs. roman,
  gap tolerance across a whole book) is a separate, still-undesigned
  piece, see the note below.
- **Done when:** against a test image with a clear arabic page number,
  returns the correct digits; against a test image with a roman
  numeral, returns the correct roman-numeral string (not `null`);
  against a test image with nothing legible in the crop region, returns
  `null`.
- **Depends on:** T25 (rotated raw images), T28 (crop rectangle),
  `tesseract` installed/available on the RPi.

**Gap found while scoping (2026-07-26): the completeness check itself
has no assigned component owner.** AD-004's 8 components explicitly
exclude "gap/consistency checking across a whole book's number
sequence" from this component's scope (it only recognizes one crop at a
time) — but that whole-sequence check (comparing every page slot's
`recognized_page_number` to detect a likely-missing page, with
tolerance for roman/arabic switches and unnumbered plates, per
`process-assumptions-audit.md`) was never assigned to any of the 8
components either. **Proposed, not yet confirmed:** the Job/Archive
Manager (§7) is the natural owner — it already assembles the full
`pages[]` array with every `recognized_page_number`, runs RPi-side
(matching the original "möglichst auf dem RPi" requirement), and is a
natural place to run this either continuously as pages arrive or once
at job finalization. Not decided here — flagging for confirmation
before scoping it as a task.

---

## Touchscreen UI Shell (Component 8)

Full background: [`reference/z1-components.md`](reference/z1-components.md)
§8. **Governing requirement (ijon, 2026-07-26): the machine is used by
laypeople with no briefing** — every screen below must carry an
explicit, plain-language instruction, never icon-only, never assuming
prior knowledge of the machine. Split into two tasks: the happy-path
flow (T30) and error/recovery/interruption screens (T31) — deliberately
separated so the recovery screens don't get shortchanged as an
afterthought to the main flow. They're arguably the more important half
given the governing requirement: a confused user with no one to ask is
exactly the failure mode it's meant to prevent.

#### T30 — Happy-path screen-flow skeleton
- **Scope — full enumerated screen sequence** (each tied to the
  component/task that drives it; exact copy/wording deferred, the
  *structure* — which screen follows which, and that each carries an
  instruction — is what this task specifies):
  1. **Boot/home — corrected 2026-07-29:** homing is **not** automatic on
     power-on — the firmware refuses to move until the user explicitly
     confirms it's OK ("Maschine bereit? Box wird nach oben fahren" →
     confirm). Only after that confirmation does T20 run homing; only
     after homing succeeds does the start screen ("Neues Buch scannen")
     appear. If homing fails or the endstop isn't reached as expected,
     this needs an error path — not detailed further here, see T31.
  2. **Cover capture** (§6, T15): "Legen Sie das geschlossene Buch mit
     der Vorderseite nach oben auf die rechte Seite" → confirm →
     capture; then "Jetzt die Rückseite nach oben auf die linke Seite"
     → confirm → capture.
  3. **ISBN check** (§6, T17): "Hat das Buch einen ISBN-Barcode? Schauen
     Sie auf dem Einband, dem Buchrücken oder den ersten/letzten Seiten
     nach" → branches: barcode found (position accordingly — reuses
     step 2's photos automatically for the cover case, no extra prompt
     needed there) / printed but no barcode (on-screen numeric keyboard)
     / none found (skip to step 5).
  4. **Metadata confirmation** (§6, T16/T18 result): show the found/
     guessed candidate with "Ist das richtig?" [Bestätigen] /
     [Manuell eingeben].
  5. **Manual metadata form** (§6, T19, fallback only): on-screen
     keyboard, clearly labeled fields, then the already-decided
     search-after-typing confirmation step.
  6. **Calibration** (§4, T27/T28): "Buch in der Mitte aufschlagen, auf
     einer Seite mit sichtbarer Seitenzahl, in die Mulde legen" →
     [Bereit] → preview shown → "Tippen Sie auf die Seitenzahl links" →
     tap → "...jetzt rechts" → tap → confirm.
  7. **Auto-Scan start**: "Bereit zum Scannen?" [Start].
  8. **Auto-Scan live screen** (§5, T11/T13): full-page preview + zoom
     preview + page counter + live pressure diagnostics, [Stop] always
     visible and reachable.
  9. **Scan-completion confirmation**: "Sind Sie fertig mit dem Scannen
     dieses Buchs?" [Ja, fertig] / [Weiter scannen].
  10. **Upload/finalization** (§7): progress indicator, then success
      confirmation (failure case is T31's, not this task's).
- **Done when:** walking through a full test book end-to-end on the
  real machine never reaches a screen without an explicit instruction
  of what to do next; a person with no prior knowledge of the machine,
  given only the touchscreen, can complete the flow (informal usability
  check, not an automated test).
- **Depends on:** T15/T16/T17/T18/T19 (§6), T27/T28 (§4), T11/T13 (§5),
  Job/Archive Manager's upload step (§7).

#### T31 — Error, recovery, and interruption screens
- **Scope:**
  1. **Pickup-retry visibility (new requirement, found 2026-07-26):**
     during Auto-Scan (T23/T24), a failed pickup attempt retries
     automatically — but a layperson watching a machine that silently
     repeats an action with no feedback would reasonably think it's
     stuck. Show a brief, visible status during a retry (e.g. "Seite
     wird erneut versucht...") rather than nothing.
  2. **3-failure choice dialog** (T14, corrected 2026-07-29 — no longer a
     plain explanation leading into the generic Stop/recovery screen):
     plain-language explanation — "Die Seite konnte nicht aufgenommen
     werden. Bitte prüfen Sie, ob die Seite frei liegt." — followed by
     three explicit choices, not just an implicit fall-through to Stop:
     [Erneut versuchen] (T14 "Try again," 3 further attempts),
     [Manuell umblättern] (T14 "Manual pageturn" — turn the page by hand,
     then confirm), [Abbrechen] (T14 "Stop job," leads into the Stop/
     recovery screen below).
  3. **Stop/recovery screen** (T9/T10): shown after any Stop (user-
     initiated, or "Stop job" chosen from T14's dialog above). Explains
     the state in plain language,
     not a bare button row: [Nach oben fahren] (T9, primary, with a
     short explanation of what it does — moves the box up so the book
     can be checked/freed) and [Nach unten fahren] (T10, secondary).
     A way back into the flow once the user is ready (resume, not
     silently auto-continuing).
  4. **Upload failure** (§7's still-open upload-retry question): error
     message + [Erneut versuchen], not a silent failure or a dead end.
- **Done when:** deliberately jamming a test page during Auto-Scan
  produces visible retry feedback, then (after 3 failures) a plain-
  language explanation and a working recovery path back to a normal
  state, without needing to consult any documentation outside the
  touchscreen itself.
- **Depends on:** T14, T9, T10, Job/Archive Manager's upload step (§7,
  including its still-open retry-behavior decision).

#### T32 — ESP32 FOC-board firmware: touchdown/positioning state machine + endstop handling
- **Scope:** custom firmware on the MKS ESP32 FOC board's own ESP32,
  built on top of SimpleFOC and its `Commander` serial interface —
  descend-until-contact touchdown, hold fully stationary at the
  compressed press value while the RPi captures photos, resume motion
  only on an explicit RPi command, startup homing, on-demand move-to-top,
  endstop handling (now on this board, not the Arduino), and the mm/force
  calibration procedures. **Fully specified in the linked requirements
  document below — not re-specified here** (an earlier version of this
  task's scope duplicated that detail inline and drifted out of sync with
  it, including a wrong "lift to scan position" step corrected 2026-07-29
  — the photo is taken at the touchdown/compressed position itself, box
  fully stationary, not after a separate lift).
- **Done when:** see the linked document's Milestone 2 (§6) — in short:
  on the real machine, lowering the box onto a physical test book stops
  on contact without operator intervention and holds a consistent,
  repeatable press force across test books of different thickness/
  binding stiffness while remaining fully stationary; motion resumes only
  after an explicit RPi "proceed" command; a move-to-top command reliably
  stops short of the endstop without Arduino involvement; homing reliably
  establishes zero and gates every other absolute-position command.
- **Depends on:** MKS ESP32 FOC board flashed with a working SimpleFOC
  base (hardware bring-up status: last confirmed via
  `project-management/planning/todo.md`/`docs/hardware/electronics.md`
  §2.4 was "blocked on board repair/replacement after the 2026-07-25
  burnout, motor not yet mechanically installed" — **re-verify with
  ijon/hrmny before assuming this is still current, don't carry this
  status forward blindly**, see `ligature-AGENTS.md`'s hardware-status
  check), endstop rewiring (`project-management/planning/todo.md`, M2,
  new 2026-07-28 item), Kt calibration for the actual motor if press
  force is to be expressed/tuned in Nm rather than raw Amps (see
  `touchdown-motion-sketch.md`).
- **Full requirements document:** [`ligature.md`](ligature.md) —
  **corrected 2026-08-02** (was pointing to
  [`reference/esp32-foc-firmware-requirements.md`](reference/esp32-foc-firmware-requirements.md)
  as primary; that document predates `ligature.md` and was superseded
  by it for protocol matters at AD-006, `architecture.md` — still useful
  for hardware-only detail, GPIO map/connector pinouts/open
  hardware-verification items, not for the command protocol itself).
  Includes the full command set (motor/board parameters via the
  reference doc, protocol via `ligature.md` itself), the binding
  "motor never moves without an explicit host command" safety
  requirement plus the `M112`/`M53`/`Fault` distinction (§3.8), the
  mm/force calibration requirement (§8), and telemetry (§12, including
  the `status` heartbeat and `Probing` state T32's implementation needs
  to actually emit). `reference/esp32-foc-firmware-requirements.md`'s
  Milestone 1 (a safe demo/jog firmware, no real page-turning) is scoped
  narrower than this task — T32 itself corresponds to that document's §6
  "Milestone 2 (Productive)". Working conventions for whoever picks this
  up (deployment, safety, code reuse, build/run): `ligature-AGENTS.md`
  — that file intentionally does not carry status, see below instead.

**Bring-up sequence and current status: `ligature.md` §17 (2026-08-05 —
moved there so it travels with the file that's self-contained; don't
duplicate it here, update it there).** Short version for this task list:
Stage 1a (old board, phases disconnected) appears complete; Stage 1b
(current-limited alignment + closed-loop current sensing) is mostly
validated but blocked on an unresolved amplifier-gain question and a
real, unresolved closed-loop motion problem (overspeed/roughness) found
by the `motor-smoke` diagnostic firmware; Stage 2 (mechanically
installed) hasn't started. No protocol-firmware implementation work has
begun yet — only ad-hoc bring-up/diagnostic firmware exists so far.

**Resolved (ijon, 2026-07-26): UI language.** MVP is **English-only**.
Dual-language (German + English) is a planned later addition, not MVP
scope — worth keeping in mind for T30/T31's implementation (e.g. avoid
baking English strings in as literals with no extraction point, so
adding German later isn't a rewrite), but no localization
infrastructure is being built now.

---

## AD-009: Start/Stop/E-Stop Button + Status LED

Full background/reasoning: `architecture.md` AD-009. Physical part:
`docs/hardware/electronics.md` §3.3.

#### T34 — Button + status-LED firmware (Arduino-side) — done (2026-08-08)

- **Done (`monospace` PR #5, `63077d9`, merged 2026-08-08):**
  implemented and verified on real hardware exactly per scope below —
  see `monospace.md` §10 for the authoritative status/detail, not
  duplicated further here.
- **Scope:** implement `monospace.md` §10 on the physical Arduino:
  debounced button-press detection on D2 (`EVENT BUTTON PRESSED`,
  §10.2), and the `LED SET <r> <g> <b>` command driving D9/D10/D11
  (§10.3). Includes the two new safety points from §3 (LED boots off,
  no exception; debounce is signal conditioning, not a third automatic
  mode). Does **not** include deciding what any color/pattern means, or
  interpreting what a button press should do — that's T35's scope
  entirely; this task only implements the wire-level contract.
- **Done when:** on the physically connected Arduino, (a) sending
  several `LED SET <r> <g> <b>` values via `arduino-cli monitor`
  produces the expected visible color/brightness, active-low as
  confirmed (§10.3 — `0`=off/`255`=full on from the host's point of
  view, inverted internally before `analogWrite`); (b) pressing the
  physical button, including deliberately sloppy/bouncy presses,
  produces exactly one `EVENT BUTTON PRESSED` line per physical press,
  never zero, never several; (c) the LED is confirmed off immediately
  after a fresh power-up/flash, before any `LED SET` has been sent.
- **Depends on:** physical wiring of the button per the pin table in
  `docs/hardware/electronics.md` §3.3 (not yet done). **No longer
  blocked on polarity** — the diode test (ijon, 2026-08-08) confirmed
  `C` = common anode, so R/G/B are active-low; that was the only
  previously-open prerequisite.
- **Full requirements:** [`monospace.md`](monospace.md) §2 (pin table),
  §3 (safety points 7/8), §4/§5 (protocol), §10 (full button/LED spec).

#### T35 — RPi Arduino-client additions + status-indicator/button-routing logic — part (a) done, part (b) not started

- **Part (a) done (`sans` PR #7, `4d8cdfa`, merged 2026-08-08):** the
  Rust Arduino client and `hw_diag` extension, verified end to end
  against the real `monospace` PR #5 firmware. Part (b) — the actual
  application-level indicator/button-routing component — has **not**
  been started; it depends on the broader `sans-serif` application
  (Auto-Scan loop, UI screen state, §1.1/§5.3/§8) existing to read state
  from, which itself isn't built yet. Don't infer that this task is done
  from part (a)'s completion.
- **Scope:** two things, both RPi-side, no firmware counterpart (unlike
  T20/T21/T22's split, there's no third file here — see the
  Board/component mapping notes above). **(a) Done:** extend the Rust
  Arduino client (`sans-serif.md` §1.2) with `set_led(r, g, b)` and the
  `on_button_press`-equivalent (shipped as `on_event`) callback, plus
  the `protocol::classify_line` addition recognizing the `EVENT ` prefix
  alongside the existing `PRESS ` one. **(b) Not started:** build the
  status-indicator/button-routing component itself (`sans-serif.md`
  §11): the state→color/pattern table (§11.1), the blink-timer loop,
  and the button-press interpretation (§11.2) wired into the existing
  Stop path (§5.3, already updated to accept this as a third trigger),
  the existing Start action (§8.1 step 7, already updated to accept
  this as an alternate trigger), and the "New job" entry point (§8.1
  steps 1–2) for a Blue-state press.
- **Done when:** on the real machine, moving through the happy-path
  screen flow (§8.1) shows the button transition Blue → Green → Amber
  → (Blue again at job end) at the right points, matching §11.1's
  table, with Amber correctly appearing for *every* automatic move
  (homing, calibration, Auto-Scan, move-to-top, jog — not just
  Auto-Scan); pressing the physical button while Blue and genuinely
  idle starts a new job exactly like the touchscreen entry point;
  pressing it while Green starts Auto-Scan exactly like tapping the
  touchscreen [Start]; pressing it while Amber stops exactly like
  tapping [Stop] (`stop()` + `all_off()`, §5.3); triggering a Stop, a
  3-failure abort, and (if reproducible) an unsolicited FOC fault each
  show the correct one of the two red patterns (§11.1); a press during
  either red state has no effect; a press while Blue but mid-job
  (cover/ISBN/metadata/calibration-setup screens) has no effect (§11.2's
  working assumption — confirm this is actually what ijon wants while
  implementing, it wasn't separately confirmed when the Blue=new-job
  answer was given).
- **Depends on:** T34 (needs the firmware's `LED SET`/`EVENT BUTTON
  PRESSED` actually working on real hardware first); the FOC client's
  `status()`/fault stream (§1.1, T20) and the Auto-Scan loop driver
  (§5.3, T24) for the state this component reads.
- **Open points from `architecture.md` AD-009, not yet resolved —
  check with ijon before finalizing this task's implementation, don't
  silently pick an answer:** the exact blink frequencies (1 Hz/4–5 Hz
  are proposals, not measured/tuned values — fine to start with,
  revisit once someone's actually looked at it running on the machine);
  the §11.2 working assumption above (Blue=new-job only applies at
  genuine idle, not mid-job) — resolved-by-assumption, not by ijon
  directly, worth a quick confirm before or during implementation.
- **Full requirements:** [`sans-serif.md`](sans-serif.md) §1.2 (client
  additions), §11 (indicator/button logic), §5.3 and §8.1 step 7 (the
  two integration points).
