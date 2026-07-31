# SimpleFOC commissioning-tool options

Reviewed: 2026-07-31

## Question

Which currently supported, first-party SimpleFOC tools and protocols can be
used to commission this ESP32 controller by inspecting telemetry and tuning
bounded motion/current-loop parameters? What do they imply for transport,
firmware integration, host-OS support, persistence/export, maintenance,
licensing, and the firmware's no-motion safety gates?

This report treats the repository's plans as desired direction, not as evidence
of tool capability. The project already pins SimpleFOC 2.4.0 in
[`platformio.ini`](../../platformio.ini); 2.4.0 is also the latest published
Arduino-FOC release found during this review, dated 2026-02-19
([release](https://github.com/simplefoc/Arduino-FOC/releases/tag/v2.4.0)).

## Decision

Use the official **SimpleFOC WebController** as the first GUI to evaluate for
interactive commissioning, but only through a project-owned, firmware-side
commissioning gate. Keep **Commander** as the underlying protocol and the
serial-terminal fallback. Do not expose SimpleFOC's stock full-motor callback
directly.

SimpleFOC Studio remains useful as a secondary experiment, especially for its
JSON save function, but it is a poorer default for SimpleFOC 2.4.0: its only
tagged release is from 2021, its current packaging does not produce a macOS
artifact, and the current source's C++ generator contains incompatible and
incorrect output. PySimpleFOC plus PacketCommander/Telemetry is promising for
future scripted tests and file export, but its own repository calls it
unfinished and not fully tested; it should not be the critical path to the
first motor test.

No evaluated GUI enforces this machine's safe current, voltage, speed, target,
alignment, or arming bounds. Those checks must remain in the ESP32 firmware.

## Option comparison

| Option | What it provides | Transport and host | Persistence/export | Status and licence | Verdict here |
|---|---|---|---|---|---|
| SimpleFOC WebController | Real-time plots; motor discovery; direct PID/LPF, limits, modes, target, current PI autotune, motor characterization, and FOC reinitialization | WebSerial over the ESP32 USB serial port. No install; requires Chromium or another browser with WebSerial. This is suitable for macOS when run in a WebSerial-capable Chromium browser. | No tuning-profile save/export is documented or implemented in the inspected UI path. Writes change the live motor object; record and persist accepted values in project-owned tooling/firmware. | Official `simplefoc`-organization fork; docs call it an **early-stage project** and claim SimpleFOC 2.3+ compatibility. The inspected head was `07e029f` dated 2026-03-09. The docs site points to MIT, but the tool repository itself has no `LICENSE` or package licence field, so redistribution terms are ambiguous. | **First GUI to evaluate**, behind a restrictive firmware adapter. Never treat its buttons or numeric widgets as safety controls. |
| SimpleFOC Studio | PyQt GUI; real-time plot; PID/LPF/limit/mode/target controls; serial terminal; JSON configuration save; Arduino-code generation | PySerial. Source installation uses Python 3.9, PyQt5, PyQtGraph, and PySerial. Current CI packages Windows and Linux only; there is no current macOS package job. A source run on macOS is plausible but unverified here. | Saves a JSON device configuration and generates a C++ snippet. It does not directly persist values to ESP32 NVS. Its generated code must not be trusted without review. | Still linked from official SimpleFOC docs and MIT licensed, but maintained in `JorgeMaker/SimpleFOCStudio`, not the SimpleFOC organization. Inspected head `6fe2896` was dated 2026-01-16; GitHub shows only one tagged release, v1.0 from 2021-04-21. | **Secondary fallback only.** Validate basic serial/plot compatibility on macOS before investing in it; do not use its generated C++ as source of truth. |
| Commander plus a serial terminal or small project host script | ASCII, G-code-like reads/writes and custom callbacks for PID, LPF, target, modes, limits, monitoring, enable/disable, and full motor configuration | Any Arduino `Stream`; normally USB serial/UART. A serial terminal is OS-independent in practice and needs no special GUI. | None by itself. Host logs or firmware-specific save/load commands must be added. | Part of maintained Arduino-FOC 2.4.0, MIT. | **Required protocol foundation and safest fallback.** Register only an allowlisted wrapper, not `commander.motor(&motor, cmd)` directly. |
| Core `motor.monitor()` telemetry | Tab-separated target, q/d voltage, q/d current, velocity, and angle; controllable variable mask and downsample | Any `Print`/serial port supported by the MCU; viewed in WebController, Studio, a serial plotter, or a custom logger | No file storage and no timestamps. The host can capture text, but it is not a deterministic high-rate trace. | Part of Arduino-FOC 2.4.0, MIT. | **Low-rate commissioning display only.** Default off, explicitly enable and downsample, then turn it off before production motion. |
| PySimpleFOC plus Arduino-FOC-drivers PacketCommander/Telemetry | Python API/CLI/Jupyter; Commander or register-based ASCII/binary serial; multi-motor telemetry; CSV/JSON/tabbed/binary output; optional CAN | Python 3.10+, PySerial; repository classifies it as OS-independent. Packet IO works with Arduino `Stream` objects. CAN support is also present, but is irrelevant to this USB-serial controller. | The telemetry CLI can write tabbed, CSV, JSON, or binary files. Arduino-FOC-drivers also has a settings abstraction, but no ESP32 NVS backend was found. | PySimpleFOC v0.0.4 says it is under active development, not fully tested, API-unstable before 1.0, not on PyPI, and has no release. Its `setup.py` says MIT, but the repo has no licence file. Arduino-FOC-drivers v1.0.9 is MIT; its telemetry README also says the telemetry code is new and not fully tested. | **Defer for first bring-up.** Reconsider for repeatable scripted sweeps/export after the safe Commander path and control loop are proven. Pin exact commits if adopted. |

Sources for the table: official [WebController documentation](https://docs.simplefoc.com/webcontroller),
[WebController repository](https://github.com/simplefoc/simplefoc-webcontroller),
[Studio documentation](https://docs.simplefoc.com/studio),
[Studio repository](https://github.com/JorgeMaker/SimpleFOCStudio),
[Commander documentation](https://docs.simplefoc.com/commander_interface),
[monitoring documentation](https://docs.simplefoc.com/monitoring),
[PySimpleFOC repository](https://github.com/simplefoc/pysimplefoc), and
[Arduino-FOC-drivers communications documentation](https://github.com/simplefoc/Arduino-FOC-drivers/blob/v1.0.9/src/comms/README.md).

## Why the WebController is the best current GUI candidate

The official WebController documentation describes a no-install WebSerial GUI
for SimpleFOC 2.3+ with real-time configuration, plotting, and multiple-motor
support. Its current source is aligned with new 2.4 functionality: it exposes
q/d current-loop gains and filters, the `FC` current-PI autotune command, the
`FP` motor-characterization command, and the `FR` FOC-reinitialization command
([current UI source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/Motors.tsx)).
The corresponding 2.4 current-loop guide documents the autotuner's prerequisites
and error checks, including known motor R/L values (or current-sense-based
characterization) and a requested bandwidth below the measured `loopFOC()`
Nyquist limit ([current-loop guide](https://docs.simplefoc.com/tuning_current_loop)).

Connection itself is compatible with a motionless boot. The inspected UI opens
the selected serial port and initially sends decimal/verbosity configuration;
motor discovery then uses `?` and parameter reads
([connection source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/SerialManager.tsx)).
It does not intentionally send an enable or target merely because the port was
opened. This is still not a firmware guarantee: the UI can toggle DTR to reset
the target, so reset must remain disarmed and motionless
([WebSerial source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/simpleFoc/serial.ts)).

The WebController has four material safety problems for this machine:

1. It deliberately exposes motor enable, target/jog controls, open-loop modes,
   a repeated target step generator, characterization, and `initFOC()` rerun.
   These are convenient general-purpose controls, not a book-scanner safety
   model.
2. Its `FocScalar` accepts any parseable number and sends it directly. Although
   callers provide `defaultMin`/`defaultMax` properties, the inspected component
   does not use those properties to validate or clamp the input
   ([scalar source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/Parameters/FocScalar.tsx)).
3. The jogging control's red **Zero** button sends target `0`; in position mode
   that means “move to angle zero,” not “stop.” The step generator's Stop button
   only cancels future browser timers and leaves the last target active
   ([jog source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/Parameters/JoggingControl.tsx),
   [step-generator source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/Parameters/StepGenerator.tsx)).
4. Motor characterization energizes phases and the official procedure requires
   the shaft to remain still; it is therefore a distinct supervised operation,
   not a harmless parameter read
   ([characterization guide](https://docs.simplefoc.com/motor_characterisation)).

Therefore the WebController should initially be used for plots and bounded
parameter changes, with its built-in jog/step/alignment/characterization paths
rejected by firmware until the matching explicit commissioning state is active.

## Why stock full-motor Commander is not the gate

Commander is the correct wire-level building block. It is ASCII-based, supports
custom callbacks, and the official API lets firmware expose a whole motor or
only selected PID, LPF, scalar, motion, and application-specific callbacks
([Commander API](https://docs.simplefoc.com/commander_interface)). The stock
full-motor command set includes current/voltage/velocity limits, direct targets,
mode switching, enable/disable, monitoring, FOC reinitialization, motor
characterization, and current PI tuning
([2.4 command list](https://docs.simplefoc.com/commands_source)).

It does not enforce board-specific bounds. In 2.4.0 the generic limit-update
methods assign their supplied values to the motor/controller fields, and PID
Commander writes assign parsed floats directly
([motor source](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp),
[Commander source](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/communication/Commander.cpp)).
Calling the stock `command.motor(&motor, cmd)` behind a GUI would therefore let
the GUI bypass this machine's current ceiling, no-motion alignment rule, and
allowed-control-mode policy.

The commissioning firmware should instead register a wrapper under the motor
identifier expected by the WebController. That wrapper should:

- allow read-only discovery/status/parameter/monitor queries while disarmed;
- accept gain/filter changes only within explicit firmware constants;
- clamp or reject current, voltage, and velocity-limit writes against immutable
  hardware/session ceilings (current remains the commissioning torque limit;
  voltage is an internal protection limit);
- reject generic target, mode, open-loop, enable, step-generator, `FR`, `FP`,
  and `FC` commands unless the corresponding supervised commissioning state is
  active;
- allow only closed-loop modes selected by the current test stage;
- clear target, disable PWM, and return to disarmed on the project stop command,
  disconnect timeout, reset, or fault; and
- keep alignment/characterization separate from ordinary arming, with explicit
  confirmation and a terminal success/failure result.

This adapter can forward individual safe reads/writes to SimpleFOC internally,
but the firmware state machine owns authorization. A GUI connection or motor
identifier is not authorization.

## Serial output and the control loop

Stock GUI integration is cooperative, not guaranteed nonblocking. The official
examples call `motor.loopFOC()`, `motor.move()`, `motor.monitor()`, and
`command.run()` from the application's loop. The current 2.4.0 source explicitly
comments that `motor.monitor()` significantly slows execution; each sample is a
series of `Print` calls
([monitor source](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L321-L381)).
`Commander::run()` drains all currently available serial bytes and executes a
completed callback inline, so neither incoming bursts nor a long callback have
a fixed execution budget
([Commander run loop](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/communication/Commander.cpp#L27-L54)).

The WebController itself recommends a downsampled monitoring rate on the order
of tens to low hundreds of messages per second, and its graph uses a sample
index rather than an ESP timestamp
([monitor UI](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/Parameters/Monitoring.tsx),
[graph source](https://github.com/simplefoc/simplefoc-webcontroller/blob/07e029fec227279e996939149219d3f2f6dd73c4/src/components/MotorMonitorGraph.tsx)).
It is useful for human tuning, not for determining control-loop jitter or exact
event timing.

For this firmware:

- set `monitor_downsample = 0` and an empty monitor-variable mask at boot;
- enable only the variables needed for the current supervised test;
- start conservatively around 10–20 messages/s and raise the rate only while
  measuring control-loop duration and missed deadlines;
- never print from a Hall ISR, current-sense ISR, or FOC timing callback;
- do not perform NVS writes, file export, sleeps, or long characterization work
  in the ordinary command/FOC service path;
- retain the planned ESP-timestamped RAM ring buffer for high-rate diagnosis,
  and transfer a completed capture only while motion is stopped or at an
  explicitly measured safe transfer rate; and
- keep production telemetry quiet: accepted/terminal command responses,
  requested status, and unsolicited faults only.

SimpleFOC also documents moving `loopFOC()`/`move()` onto an ESP32 hardware
timer while leaving monitoring and Commander in the main loop
([real-time-loop guide](https://docs.simplefoc.com/real_time_loop)). That is an
available architecture, not an automatic recommendation here: it must first be
validated against this board's Hall interrupts, ESP32 core/toolchain version,
PWM/current-sense timing, and stop latency.

The existing Hall-validation note also records a 2.4.0 long-idle velocity risk;
GUI velocity traces do not resolve it. Closed-loop velocity commissioning still
needs the documented soak/fix evidence in
[`docs/hall-validation.md`](../hall-validation.md#known-simplefoc-velocity-risk).

## Persistence and reproducibility

All tuned values should be considered volatile until the project explicitly
accepts them. The recommended flow is:

1. Query and record the complete before-state.
2. Change one bounded value or one coherent gain set.
3. Record the exact command, accepted value, firmware/library versions, load,
   current/voltage ceilings, and capture identifier.
4. When a profile passes the relevant motor/lift-axis test, save it to a
   project-owned human-reviewable file and compile it as the default or persist
   it through a distinct, disarmed firmware save command.
5. On load, validate record version/integrity and reapply hard firmware caps;
   stored values never override safety ceilings.

Studio can save JSON, but its generated source is not trustworthy for this
project. In the inspected current source, it emits the nonexistent 2.4.0 field
`motor.sensor_electrical_offset` instead of `zero_electric_angle`, and it writes
`sensorElectricalZero` into `motor.phase_resistance`
([Studio generator](https://github.com/JorgeMaker/SimpleFOCStudio/blob/6fe2896155bc916489151581aa19597fbc7ba863/src/simpleFOCConnector.py#L285-L386),
[2.4 motor fields](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.h)).
Use its JSON only as an experiment log after schema validation; do not paste its
generated code into firmware.

## Advanced scripted option

Arduino-FOC-drivers v1.0.9 provides register-based PacketCommander and
Telemetry over ASCII or binary Arduino Streams. Its telemetry can select more
registers than core monitoring, control downsampling and minimum elapsed time,
and describe each stream with a header
([communications README](https://github.com/simplefoc/Arduino-FOC-drivers/blob/v1.0.9/src/comms/README.md),
[telemetry README](https://github.com/simplefoc/Arduino-FOC-drivers/blob/v1.0.9/src/comms/telemetry/README.md)).
PySimpleFOC consumes Commander or the packet protocols and includes a telemetry
CLI intended to write CSV, JSON, tabbed, or binary files
([PySimpleFOC README](https://github.com/simplefoc/pysimplefoc/blob/bf942b5bdae146bcf674923ec1b40a658357f676/README.md),
[telemetry CLI](https://github.com/simplefoc/pysimplefoc/blob/bf942b5bdae146bcf674923ec1b40a658357f676/utilities/simplefoc-telemetry.py)).

This is not currently the low-risk path. PySimpleFOC's own warnings say it is
unreleased and unstable, and the inspected code still contains work-in-progress
paths and obvious misspelled member references in some setters
([Commander client source](https://github.com/simplefoc/pysimplefoc/blob/bf942b5bdae146bcf674923ec1b40a658357f676/simplefoc/commander.py)).
It can be revisited after first bring-up if a repeatable host-side parameter
sweep/export tool becomes more valuable than the additional firmware and host
dependencies.

## Commissioning acceptance checks for whichever GUI is selected

Before the selected GUI is allowed near the assembled lift axis, demonstrate:

- opening, closing, and reconnecting the serial port cannot enable PWM or move;
- browser DTR reset and ESP32 reset return to disabled with no alignment;
- all over-limit current/voltage/velocity/gain/target writes are rejected or
  clamped and the accepted value is reported;
- built-in GUI Zero, jog, step, enable, open-loop, reinit, characterize, and
  autotune actions cannot bypass the firmware state gate;
- immediate stop preempts GUI traffic, disables PWM, disarms, and invalidates
  homing under the current conservative policy;
- continuous GUI polling and the chosen plot rate do not violate measured FOC
  loop or Hall-edge service budgets;
- unplugging or killing the GUI leaves a bounded, defined state rather than a
  persistent step/velocity/current target; and
- a recorded tuning profile can be reproduced after power cycle without using
  unreviewed GUI-generated source.

## Uncertainty and follow-up

This is a documentation and source review, not a device test. The WebController
was not exercised with the MKS ESP32 FOC board on macOS, Studio was not run with
Python/PyQt on macOS, and serial throughput/control-loop interference was not
measured on this firmware. The first implementation ticket should therefore be
a motionless compatibility spike: expose read-only discovery and low-rate
monitoring through the bounded adapter, connect the WebController from the
actual macOS host, and measure loop timing before enabling any GUI-originated
motion or calibration command.
