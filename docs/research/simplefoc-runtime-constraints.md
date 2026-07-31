# SimpleFOC 2.4.0 runtime constraints

Research date: 2026-07-31

## Scope and evidence boundary

This report answers the startup, Hall-sensor, inline-current-sense, and telemetry questions that must be settled before the ESP32 motor test and production firmware are designed. It treats the repository plans as desired behavior, not as evidence that the hardware or library already behaves that way.

The source-of-truth baseline is the exact `askuric/Simple FOC @ 2.4.0` dependency selected by this repository. The tagged source is commit [`4f072b365f6e0185adca544071e595834405babc`](https://github.com/simplefoc/Arduino-FOC/tree/v2.4.0). Live SimpleFOC documentation is useful for public API intent, but it can describe post-2.4.0 behavior; where it differs, the tag wins. The repository's existing Hall procedure establishes only the sensor-only observations documented in [`docs/hall-validation.md`](../hall-validation.md). It does not validate bridge drive, commutation, current sensing, closed-loop current, or motion control.

The `simplefoc-hall-validation` environment compiled successfully during this research with SimpleFOC 2.4.0, PIOArduino platform 55.3.39, Arduino-ESP32 3.3.9, and ESP-IDF libraries 5.5.4. That is compilation evidence, not a new device test.

## Conclusions at a glance

| Requirement | SimpleFOC 2.4.0 support | Project action |
| --- | --- | --- |
| Restart without moving through FOC alignment | Supported when both sensor calibration values and the final current-sense mapping/signs are restored | Store a versioned commissioning record; reject it on mismatch; keep the application disarmed until all checks pass |
| Keep the bridge-enable pin low for the whole startup | Not supported by unmodified `BLDCMotor::init()` | Either accept its brief zero-PWM enable as a tested requirement, or wrap/patch initialization so the bridge is never asserted before arm |
| Make commissioning an explicit, stoppable operation | Alignment exists, but `initFOC()` is synchronous and blocking | Put commissioning in an application state machine and provide a stop path independent of the blocked calibration call; a cooperative library patch is preferable |
| Use Hall velocity after long idle | Broken in the tagged implementation at the signed 32-bit microsecond boundary | Vendor the equivalent of open PR [#469](https://github.com/simplefoc/Arduino-FOC/pull/469) and test both wrap boundaries |
| Detect illegal or skipped Hall states | Not provided by `HallSensor` | Retain an application-level raw Hall sequence validator or patch the sensor class |
| Trust example current scale and polarity | No | Identify the populated amplifier and calibrate ampere scale, polarity, phase map, noise, and saturation on the actual board |
| Capture control-loop telemetry without disturbing control | Supported through cached public fields and a small subclass hook | Copy fixed-size numeric samples into a preallocated RAM ring; do not format or transmit serial data in the control loop |

## Motionless startup from stored calibration

SimpleFOC already exposes the two motor/sensor calibration results needed to skip mechanical alignment: `motor.sensor_direction` and `motor.zero_electric_angle`. During [`initFOC()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L773-L843), [`alignSensor()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L846-L924) skips its direction sweep when `sensor_direction` is not `UNKNOWN`, and skips the energized electrical-zero step when `zero_electric_angle` is set. The official documentation describes the same public mechanism under [skipping alignment](https://docs.simplefoc.com/foc_implementation#skipping-alignment).

Current-sense alignment is independent. It is skipped only by restoring the final phase-pin mapping and gain signs and setting [`current_sense.skip_align = true`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/CurrentSense.cpp#L143-L172). Restoring only the two motor fields does not make startup motionless if current-sense alignment still runs.

The commissioning record should therefore include at least:

- a schema version plus board revision, motor identity, pole-pair count, Hall pin order, driver channel, current-sense pins, shunt/gain assumption, and relevant firmware/library version;
- `sensor_direction` and `zero_electric_angle`;
- the final `pinA`/`pinB`/`pinC` ordering and `gain_a`/`gain_b`/`gain_c` signs produced by current-sense alignment;
- an integrity check and a completed/valid marker written only after the entire commissioning operation succeeds.

Do not persist ADC zero offsets. [`InlineCurrentSense::init()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/current_sense/InlineCurrentSense.cpp#L32-L87) averages 1000 zero-current readings to calculate per-pin offsets, so they should be recalculated at every boot with the bridge physically disabled. Restore pin order and gain signs before calling `init()` so each newly measured offset remains associated with the correct pin.

A safe application-owned startup sequence is:

1. Drive the physical bridge-enable pin inactive as early as possible.
2. Initialize the Hall inputs and interrupts, then validate the raw state and the versioned commissioning record.
3. Initialize the driver and immediately disable it.
4. Configure the motor, link its driver, explicitly set both feed-forward vectors to `{0, 0}`, call `motor.init()`, and immediately disable it. If the strict bridge-low requirement applies, use the patched/wrapped initialization described below instead.
5. Configure the restored current-sense pin order and gain signs, initialize it with the bridge disabled, and link it to the motor.
6. Restore `sensor_direction` and `zero_electric_angle`, set `skip_align`, and call `initFOC()` while the motor is disabled. Treat any false result as a latched calibration fault.
7. Remain disarmed. On an ordinary arm request, initialize angle target to the current shaft angle and current target to zero before enabling the bridge.

There is one library obstacle to a strict “enable pin never asserts during boot” requirement: [`BLDCMotor::init()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/BLDCMotor.cpp#L65-L118) waits 500 ms, calls `enable()`, and waits another 500 ms. [`enable()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/BLDCMotor.cpp#L120-L138) asserts the driver and writes zero PWM. Immediate `motor.disable()` makes this brief, but cannot prove that the bridge stayed inactive. If never asserting enable is a hard requirement, add a small local initialization wrapper/patch or defer `motor.init()` until explicit arm. This behavior must be checked against the actual MKS driver enable circuit.

Application gating is also mandatory. Tagged 2.4.0 [`loopFOC()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L578-L603) returns for calibration and for `!enabled`, but does not require `motor_status == motor_ready`. The firmware must enforce an `ArmedAndCalibrated` state rather than treating SimpleFOC's status as the sole safety interlock.

Stored electrical calibration does not preserve the scanner's mechanical/home coordinate. [`HallSensor::init()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/sensors/HallSensor.cpp#L142-L168) resets its accumulated rotations, and the six-state Hall pattern is not an absolute multi-turn position sensor. A reset or loss of trusted position still requires re-homing.

## Separate commissioning operation

SimpleFOC has no first-class nonblocking commissioning mode. `initFOC()` performs all required work synchronously. When values are unknown, sensor alignment executes two 501-step sweeps with 2 ms delays plus settling, then holds the rotor for approximately 700 ms to measure electrical zero. Current-sense alignment subsequently energizes phases in several 300 ms ramps and waits; its implementation is in [`CurrentSense::alignBLDCDriver()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/CurrentSense.cpp#L175-L407).

The production protocol should therefore distinguish two operations:

- **Commission**: allowed only under explicit service conditions, with conservative alignment voltage/current, hardware power removal available, an untrusted-position state, and no ordinary motion commands. Begin from `Direction::UNKNOWN`, unset electrical zero, baseline current pin mapping/gains, and `skip_align = false`. Persist the final fields only after `initFOC()` returns success and post-checks pass, then disable immediately.
- **Start/arm**: restore a compatible record and skip every energized alignment step. If the record is absent, incompatible, or corrupt, refuse to arm and request commissioning; do not silently calibrate.

The built-in Commander reinitialization command is unsuitable as the production interface: [`FR`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/communication/Commander.cpp#L283-L293) clears the two motor alignment fields and calls `initFOC()` synchronously. It does not provide cooperative cancellation or the project's record/state checks.

A serial stop command cannot preempt a control task blocked inside unmodified `initFOC()`. At minimum, an independent highest-priority stop path must be able to pull the hardware enable low even while calibration continues; after such a stop, discard all calibration results and latch an aborted fault. The more robust production solution is a small cooperative alignment state machine (local patch or project-owned equivalent) that advances one bounded step per control iteration and checks the stop latch between steps. Both designs need a physical test proving stop latency during every alignment phase. Software must not replace the external power-cut method during first commissioning.

## Hall direction, position, and velocity

Tagged `HallSensor` sets `cpr = 6 * pole_pairs`, uses a fixed six-sector lookup, and derives its transition direction from sector deltas in [`HallSensor.cpp`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/sensors/HallSensor.cpp#L9-L79). With the repository's currently documented four-pole-pair motor this is 24 edges per mechanical revolution. `HallSensor.direction` is the logical sign produced by the chosen Hall pin order and sector table; it is not an independently established physical “scanner forward” direction. `motor.sensor_direction` is the separate phase/sensor orientation learned by FOC alignment. The application must map both to named machine directions during commissioning.

Velocity is a quantized one-Hall-edge estimate divided by the last same-direction edge period. [`getVelocity()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/sensors/HallSensor.cpp#L125-L140) returns zero if no valid period exists or if the last edge is older than twice the recorded period. After a direction reversal, `updateState()` deliberately clears the period, so a stable nonzero estimate requires another same-direction transition. At low speed, velocity will update only on Hall edges and then fall to zero using that adaptive timeout; position control must tolerate this quantization rather than interpreting it as a high-resolution tachometer.

Two tagged-2.4.0 defects matter:

1. The timestamp and elapsed locals are signed `long`, and a timed-out `pulse_diff` is not cleared. After the signed 32-bit microsecond boundary (about 35.8 minutes), a stationary sensor can expose stale nonzero velocity. Open PR [#469, “Fix HallSensor velocity calculation on micros() overflow”](https://github.com/simplefoc/Arduino-FOC/pull/469) changes these values to unsigned arithmetic and clears the stale period. That patch is not in the v2.4.0 tag and should be vendored equivalently before relying on Hall velocity.
2. Hall states `000` and `111` map to an invalid sector, but `updateState()` has no explicit legal-state, one-bit-transition, or adjacent-sector fault. It can turn an invalid or skipped transition into direction/rotation changes. The existing raw Hall validation logic should become a production fault guard, or the library class should be patched to reject and report invalid transitions.

Although the v2.4.0 header declares [`velocity_max`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/sensors/HallSensor.h#L79-L80), the tagged Hall implementation never reads it. The current documentation's [velocity outlier setting](https://docs.simplefoc.com/hall_sensors#step-21-velocity-outlier-removal) must not be assumed effective in this pinned version.

## ESP32 inline current sense

The public constructor [`InlineCurrentSense(shunt, gain, pinA, pinB, pinC)`](https://docs.simplefoc.com/inline_current_sense) converts voltage to current with `1 / (shunt * gain)` amperes per volt. The required order is driver initialization, link the driver to current sense, current-sense initialization with a checked return value, link current sense to the motor, then `initFOC()`.

Current-sense alignment is useful during commissioning, but it establishes only relative phase mapping and sign. [`driverAlign()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/CurrentSense.cpp#L143-L172) can return success codes 1 through 4, and the BLDC alignment can swap public pin/offset/gain fields and negate gain signs. `FOCMotor::alignCurrentSense()` collapses that to boolean success, so record the final public fields after a successful commissioning run. Automatic alignment does not prove the absolute amperes-per-count scale.

On classic ESP32, SimpleFOC 2.4.0 configures 12-bit ADC sampling with 11 dB attenuation and converts raw counts using a nominal `3.3 / 4095` V/count. Its [ESP32 ADC setup](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/current_sense/hardware_specific/esp32/esp32_mcu.cpp#L8-L35) and [IRAM ADC reader](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/current_sense/hardware_specific/esp32/esp32_adc_driver.cpp#L5-L95) use direct-register reads protected against core migration, but do not apply per-chip Vref or ADC-linearity calibration. Measure scale, offset/noise, linearity, and clipping against a known current/DMM over the intended operating range.

The MKS first-party evidence is internally inconsistent and cannot establish production constants. The manufacturer's [current-control example](https://github.com/makerbase-motor/MKS-ESP32FOC/blob/MKS-ESP32-FOC-V1.0/Test%20Code/7_current_control_example/7_current_control_example.ino) constructs two-channel sensing on GPIO 39/36 with `0.01 ohm` and `50 V/V`, then negates both gains. Its [standalone current-sense example](https://github.com/makerbase-motor/MKS-ESP32FOC/blob/MKS-ESP32-FOC-V1.0/Test%20Code/9_online_current_sense_test/9_online_current_sense_test.ino) uses the same nominal shunt/gain but negates only phase B. The repository's first-party [MKS schematic](../pdf/MKS%20ESP32%20FOC%20V1.0%20schematic.pdf) labels 0.01-ohm shunts and INA181A1 current amplifiers, while TI specifies [INA181A1 as 20 V/V](https://www.ti.com/lit/ds/symlink/ina181.pdf), not 50 V/V. Inspect the populated part markings and calibrate the actual board. Until that is done, neither closed-loop current nor a current-limit safety claim is established.

## Nonblocking control-loop capture

Do not call `motor.monitor()` during active motion. Tagged [`FOCMotor::monitor()`](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.cpp#L319-L364) formats values and writes through Arduino `Print`; the official [monitoring documentation](https://docs.simplefoc.com/monitoring) warns that monitoring reduces motion-control performance.

For the motor test and tuning evidence, copy numeric values after each control iteration into a fixed-size, preallocated RAM ring buffer. A useful record includes a 64-bit ESP timestamp, sequence number, Hall state/sector, shaft angle and velocity, angle/velocity targets, `current_sp`, cached q/d currents, cached q/d voltages, electrical angle, loop time, controller state, fault bits, and overrun/drop count. [`esp_timer_get_time()`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html#obtaining-current-time) provides fast 64-bit microsecond timestamps without locks. The hot path must not allocate, format floats, acquire a serial mutex, or transmit. Drain the frozen buffer only after disable/fault/test completion; production serial output can remain limited to compact terminal success/failure records.

If exact raw phase currents used by FOC are required, do not perform a second ADC conversion merely for logging. `CurrentSense::getPhaseCurrents()` is a [virtual hook](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/CurrentSense.h#L73-L99). A small `InlineCurrentSense` subclass can override it, call the base implementation once, cache that returned sample, and return it to FOC; the application then copies the cache into the ring after `loopFOC()`.

## Compatibility and local patches

SimpleFOC's [2.4.0 release notes](https://github.com/simplefoc/Arduino-FOC/releases/tag/v2.4.0) state compatibility with Arduino-ESP32 3.x and describe ESP32 IRAM/core-migration ADC/PWM fixes. The repository pins SimpleFOC itself, but its PIOArduino platform URL follows `stable`. Pin the tested platform release before production qualification so compiler/core/ADC behavior cannot drift underneath an unchanged firmware revision.

The following changes are required or should be decided before production motion work:

- **Required:** vendor the Hall timer-wrap/stale-period fix equivalent to PR #469 and add long-idle tests.
- **Required:** preserve an application-level illegal/skipped Hall transition detector and latch it as a drive fault.
- **Required:** implement application safety/commissioning states; never expose raw Commander reinitialization as an ordinary operation.
- **Required:** ensure stop can deassert bridge enable independently of blocking alignment; prefer cooperative/cancellable commissioning for the final design.
- **Required:** explicitly assign `motor.feed_forward_current = {0, 0}` and `motor.feed_forward_voltage = {0, 0}`. These members lack in-class initializers in the [2.4.0 header](https://github.com/simplefoc/Arduino-FOC/blob/v2.4.0/src/common/base_classes/FOCMotor.h#L211-L224), the subject of open issue [#522](https://github.com/simplefoc/Arduino-FOC/issues/522). A tiny local initializer patch is also reasonable.
- **Decision:** patch/wrap `BLDCMotor::init()` if the safety requirement is “bridge enable never asserted before arm,” rather than merely “no commanded motion during startup.”
- **Not required:** an algorithm patch to skip stored sensor/current alignment; the existing public fields support it.
- **Not required:** continuous SimpleFOC text monitoring; a project-owned RAM capture path is both simpler and less intrusive.

## Hardware validation gates

Compilation cannot resolve the remaining physical uncertainties. Before position/current control is treated as ready, run and retain evidence for:

1. **Board identity:** populated current amplifier part, shunt value, bridge channel, enable polarity, current pins, phase-to-pin mapping, and Hall-to-machine direction mapping.
2. **Current metrology:** zero-offset repeatability over cold boots, sign and phase mapping, calibrated A/count slope versus a DMM or known load, linearity/noise, ADC/amplifier saturation, and conservative overcurrent threshold.
3. **Commissioning safety:** safe alignment voltage/current, complete result record, record mismatch/corruption rejection, power loss during every phase, and stop-to-enable-low latency while the library alignment call is active.
4. **Motionless startup:** repeated power cycles with a valid record show no alignment motion; invalid/missing records remain disarmed; ordinary arm begins from zero current and present angle.
5. **Hall behavior:** controlled-speed direction/velocity comparison, reversals, skipped/illegal transitions, idle-to-zero behavior, an idle soak beyond 45 minutes to cross signed wrap, and preferably beyond 75 minutes to cross full 32-bit microsecond wrap.
6. **Mechanical position:** every reset/loss-of-trust requests re-home; stored electrical calibration is never mistaken for absolute scanner position.
7. **Timing:** control-loop frequency and jitter with capture enabled, buffer overrun accounting, no serial work in the hot path, and terminal-only production output.

Only after these gates should the project tune current and position loops or assign a user-facing current limit. Voltage limits remain internal current-loop protection and cannot compensate for an unverified current scale.
