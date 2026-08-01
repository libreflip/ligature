# Inventory the lift-axis hardware and test safeguards

Type: task
Status: resolved

## Question

What is physically present and verified for the next powered tests: motor channel, PWM wiring and hardware isolation, phase connections, current-sense pins/shunt/gain and polarity, supply limits, Hall power and mapping, endstop pin/type/polarity, lift-axis assembly state, representative loads, and the operator's immediate means of removing motor power? Record observations and measurements separately from values copied from the directional plans.

## Comments

### Inventory worksheet — 2026-07-31

The following facts have current device evidence in `docs/hall-validation.md`:

- The tested combination is an MKS ESP32 FOC V1.0 and StepperOnline 42BSA78-24-01 motor.
- Hall A/B/C are connected to `SDA_1`/GPIO 23, `SCL_1`/GPIO 5, and `I_1`/GPIO 13. The Hall sensors use 5 V power and an external 3.3 V pull-up board.
- Both directions produced the expected six-state Hall cycle with 24 valid edges per shaft revolution and no invalid states or transitions during the guided captures.
- The validation firmware drove GPIO 22 and GPIO 21 low and did not initialize PWM or a motor driver. The schematic marks those GPIOs unconnected, so the writes had no bridge-control effect. The phase leads were disconnected, so this did not validate either bridge, PWM routing, phase order, current sense, or powered motion.

The motor manufacturer drawing in `docs/pdf/42BSA78-24-01.pdf` documents, but the device test has not independently measured, an eight-pole three-phase 24 V motor, 4.80 A rated current, 1.03 ohm phase resistance, 0.43 mH phase inductance, Hall wires red/+5 V, black/GND, yellow/A, green/B, blue/C, and phase wires yellow/U, green/V, blue/W.

The board schematic documents 0.01-ohm shunts and INA181A1 current amplifiers,
with channel-0 ADC inputs on GPIO 39/36 and channel-1 inputs on GPIO 35/34.
On 2026-08-04, the operator inspected both Motor 1 amplifier packages and
confirmed the `1AED` top mark, which TI assigns to INA181A2 (50 V/V). This
resolves the populated Motor 1 amplifier variant in favor of the upstream MKS
example and against the schematic's A1 label. It does not calibrate the shunt,
ESP32 ADC, phase association, polarity, offsets, noise, linearity, or saturation.

The following still require current observations from the assembled machine before this ticket can resolve:

1. Board condition and identity: whether the previously mentioned damaged board was repaired or replaced, its visible revision/markings, and whether the Hall-validated board is the board intended for powered testing.
2. Motor channel and phase wiring: M0 or M1; the three motor phase wire colors and their exact board terminal labels/order; whether those leads are presently disconnected or connected.
3. Driver routing: the intended channel's three PWM GPIOs and the presence or absence of any hardware isolation/control input, confirmed from the actual board/vendor mapping before energizing.
4. Current-sense hardware: readable shunt markings and any current-scale or
   polarity measurements; both Motor 1 amplifier markings and GPIO 35/34 are
   now identified.
5. Supply: supply model, set voltage, adjustable current limit and its present setting, fuse/protection if any, and whether logic/USB and motor power can be controlled independently.
6. Endstop: whether it is installed at the upper end, its switch type, NO/NC contact choice, exact ESP32 pin/header, pull-up/down, asserted logic level, and a PWM-off manual trigger reading.
7. Mechanical state: motor-only, motor+gearbox, or complete gearbox/belt/suction-box lift axis; whether motion is unobstructed through its intended range; any known hard limits or damaged parts.
8. Test loads: which representative books or equivalent loads are available, including a thin/flexible and a thick/stiff case if possible.
9. Immediate power removal: the exact physical action that removes motor power, where it is located, and whether the operator can keep it within reach throughout the test.

Photos of the board power stage/current-sense area, motor terminals, supply controls, endstop wiring, and full lift assembly can answer most visual items without relying on plan text.

### Operator report — 2026-07-31

- Motor 0 is permanently unavailable because two of its MOSFETs were removed. Firmware and test procedures must prohibit Motor 0 and use only the Motor 1 output.
- The validated Hall wiring is already on the matching Motor 1 `ENCODER_1` input, as documented in `docs/hall-validation.md` and `src/hall_validation/main.cpp`.
- The currently disconnected motor phases will be wired motor U/V/W to board A1/B1/C1 respectively before the powered test.
- The local board schematic maps Motor 1 phase PWM A1/B1/C1 to GPIO 26/27/14 and its current-sense inputs to GPIO 35/34. It marks GPIO 21 and GPIO 22 unconnected and shows no separate motor-driver enable net. These are schematic facts; the first test still has to confirm phase/current polarity, scale, boot/reset PWM behavior, and powered behavior on the surviving channel.
- The motor supply is a Mean Well LRS-150-24. For the current bench setup, the operator will remove its mains plug to physically remove motor power and will keep the plug immediately reachable during powered tests. Supply-side fuse/protection beyond the power supply's own behavior has not been established.
- The endstop switch is connected between GND and `I_0`; the board schematic maps `I_0` to GPIO 4. Its NO/NC contact choice and asserted logic level remain to be checked with all PWM commands off.
- The next stage is standalone unloaded-motor testing. The gearbox, belt, suction-box lift axis, and representative books are intentionally not part of this first powered stage and are not yet available for validation.

The `I_0` switch may be used as a fail-fast software stop/interlock during bench testing once its contact behavior is verified. It is not an emergency stop: it depends on the ESP32, input wiring, firmware, and zero-PWM stop path continuing to work. A separate operator-reachable action that physically removes motor power remains a prerequisite for energizing the motor.

## Answer

The first powered stage is restricted to the standalone, unloaded StepperOnline 42BSA78-24-01 motor on Motor 1 of the Hall-validated MKS ESP32 FOC V1.0 board. Motor 0 is prohibited because two of its MOSFETs were removed. No gearbox, belt, suction box, lift-axis mechanism, or representative book is present for this stage.

Motor 1 uses the already validated `ENCODER_1` Hall inputs: A/B/C on GPIO 23/5/13, 5 V Hall supply, and external 3.3 V pull-ups. Before powered testing, motor U/V/W will be connected to board A1/B1/C1. The schematic maps these PWM outputs to GPIO 26/27/14 and current-sense inputs to GPIO 35/34. Both populated Motor 1 current amplifiers are INA181A2 (50 V/V), identified by their `1AED` top marks. The board provides no separate motor-driver enable input; GPIO 21 and GPIO 22 are unconnected. Phase/current polarity, absolute current scale, offsets, noise, saturation, boot/reset PWM behavior, and powered behavior remain unverified inputs to the smoke test rather than established facts. A successful smoke test may show plausibility but cannot turn them into calibrated measurements.

The supply is a Mean Well LRS-150-24. The operator-reachable physical power-removal action for this bench stage is pulling its mains plug, which must remain immediately accessible whenever the motor is energized. The endstop is connected between GND and `I_0`/GPIO 4. It may serve as a latched software stop/interlock after a PWM-off contact/polarity check, but it is not an emergency stop and cannot replace pulling mains power.

This inventory is sufficient to decide the unloaded-motor bring-up procedure.
The current procedure lives in [Define the simple unloaded-motor powered
bring-up and smoke
test](04-define-safe-powered-bring-up-and-motor-test-ladder.md); its 2026-08-01
revision accepts the replaceable bridge/motor risk and no longer requires the
earlier phase-disconnected, `I_0`, and current-baseline gate ladder. Motor 0
remains prohibited and the mains plug remains the supervised physical
power-removal method.

### Schematic correction — 2026-08-01

The earlier inventory incorrectly treated GPIO 21 as Motor 1 enable and GPIO
22 as Motor 0 enable. The checked-in V1.0 schematic marks both pins unconnected
and shows no separate enable net on either EG2133 driver block. Firmware can
command all phase PWMs low/zero, but only the operator's mains action isolates
bridge power.
