# Establish SimpleFOC startup, Hall, and current-sense constraints

Type: research
Status: resolved
Research context: change `wqlwzkon` → [runtime-constraints research](../../../docs/research/simplefoc-runtime-constraints.md)

## Question

Against current first-party SimpleFOC documentation and source, what supported APIs or required patches govern motionless startup from stored sensor direction/electrical zero, HallSensor direction and velocity behavior including long-idle timer wrap, current-sense alignment/scaling/polarity, current-loop monitoring, and the separation of explicit calibration from ordinary arming on this ESP32 target?

## Answer

SimpleFOC 2.4.0 can skip energized sensor alignment only when a compatible record restores both `sensor_direction` and `zero_electric_angle`; current-sense alignment is separate and requires restoring its final phase-pin mapping and gain signs with `skip_align = true`. ADC zero offsets should still be recalculated on every boot with all PWM commands off and no phase current. Electrical calibration cannot restore the lift axis's multi-turn mechanical/home coordinate, so reset or lost position trust still requires homing.

Unmodified `BLDCMotor::init()` briefly sets the motor's software state enabled while writing zero PWM. The V1.0 schematic has no physical bridge-enable signal: GPIO 21/22 are unconnected. Firmware must establish low phase-control latches before PWM initialization and test the boot/reset transition for unintended pulses. Unknown-value `initFOC()` and current-sense alignment are synchronous and blocking, so the final commissioning design needs cooperative/cancellable alignment; there is no hardware-enable fallback for a serial stop while the call is blocked.

Before relying on Hall velocity, vendor the equivalent of upstream PR #469's unsigned timer-wrap/stale-period fix and retain an application-level illegal/skipped-transition fault detector. The MKS examples and schematic disagree on current-sense gain/polarity, so the populated amplifier, shunts, phase mapping, ampere scale, noise, linearity, and saturation must be measured on this board. Current remains the user-facing torque limit; voltage is only an internal protection clamp.

High-rate diagnostics must copy fixed-size numeric samples with ESP timestamps into preallocated RAM, never format or transmit serial data in the control path. The complete API/source evidence and device-validation gates are in the [runtime-constraints research](../../../docs/research/simplefoc-runtime-constraints.md). Its successful `simplefoc-hall-validation` build is compilation evidence only, not powered-hardware validation.

## Correction — 2026-08-01

The original answer assumed dedicated bridge-enable pins. Schematic review
showed that GPIO 21/22 are unconnected and neither EG2133 block has a separate
enable net. Software `enable`/`disable` terminology now refers only to active
control state and zero PWM, not bridge-power isolation.
