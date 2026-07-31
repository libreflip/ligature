# Inventory the lift-axis hardware and test safeguards

Type: task
Status: claimed

## Question

What is physically present and verified for the next powered tests: motor channel, PWM/enable wiring, phase connections, current-sense pins/shunt/gain and polarity, supply limits, Hall power and mapping, endstop pin/type/polarity, lift-axis assembly state, representative loads, and the operator's immediate means of removing motor power? Record observations and measurements separately from values copied from the directional plans.

## Comments

### Inventory worksheet — 2026-07-31

The following facts have current device evidence in `docs/hall-validation.md`:

- The tested combination is an MKS ESP32 FOC V1.0 and StepperOnline 42BSA78-24-01 motor.
- Hall A/B/C are connected to `SDA_1`/GPIO 23, `SCL_1`/GPIO 5, and `I_1`/GPIO 13. The Hall sensors use 5 V power and an external 3.3 V pull-up board.
- Both directions produced the expected six-state Hall cycle with 24 valid edges per shaft revolution and no invalid states or transitions during the guided captures.
- The validation firmware drove GPIO 22 and GPIO 21 low and did not initialize PWM or a motor driver. The phase leads were disconnected, so this did not validate either bridge, either enable polarity, PWM routing, phase order, current sense, or powered motion.

The motor manufacturer drawing in `docs/pdf/42BSA78-24-01.pdf` documents, but the device test has not independently measured, an eight-pole three-phase 24 V motor, 4.80 A rated current, 1.03 ohm phase resistance, 0.43 mH phase inductance, Hall wires red/+5 V, black/GND, yellow/A, green/B, blue/C, and phase wires yellow/U, green/V, blue/W.

The board schematic and upstream research document 0.01-ohm shunts and INA181A1 current amplifiers, with channel-0 ADC inputs on GPIO 39/36 and channel-1 inputs on GPIO 35/34. This does not establish what is populated on this board, the absolute ampere scale, phase association, polarity, offsets, noise, linearity, or saturation. The directional plan's 50 V/V assumption conflicts with the schematic's INA181A1 part and must not become a firmware constant without inspection and measurement.

The following still require current observations from the assembled machine before this ticket can resolve:

1. Board condition and identity: whether the previously mentioned damaged board was repaired or replaced, its visible revision/markings, and whether the Hall-validated board is the board intended for powered testing.
2. Motor channel and phase wiring: M0 or M1; the three motor phase wire colors and their exact board terminal labels/order; whether those leads are presently disconnected or connected.
3. Driver routing: the intended channel's three PWM GPIOs and physical enable GPIO/polarity, confirmed from the actual board/vendor mapping before energizing.
4. Current-sense hardware: readable amplifier and shunt markings or clear close-up photos; intended channel ADC pins; any prior current-scale or polarity measurements.
5. Supply: supply model, set voltage, adjustable current limit and its present setting, fuse/protection if any, and whether logic/USB and motor power can be controlled independently.
6. Endstop: whether it is installed at the upper end, its switch type, NO/NC contact choice, exact ESP32 pin/header, pull-up/down, asserted logic level, and a bridge-disabled manual trigger reading.
7. Mechanical state: motor-only, motor+gearbox, or complete gearbox/belt/suction-box lift axis; whether motion is unobstructed through its intended range; any known hard limits or damaged parts.
8. Test loads: which representative books or equivalent loads are available, including a thin/flexible and a thick/stiff case if possible.
9. Immediate power removal: the exact physical action that removes motor power, where it is located, and whether the operator can keep it within reach throughout the test.

Photos of the board power stage/current-sense area, motor terminals, supply controls, endstop wiring, and full lift assembly can answer most visual items without relying on plan text.

### Operator report — 2026-07-31

- Motor 0 is permanently unavailable because two of its MOSFETs were removed. Firmware and test procedures must prohibit Motor 0 and use only the Motor 1 output.
- The validated Hall wiring is already on the matching Motor 1 `ENCODER_1` input, as documented in `docs/hall-validation.md` and `src/hall_validation/main.cpp`.
- The currently disconnected motor phases will be wired motor U/V/W to board A1/B1/C1 respectively before the powered test.
- The local board schematic maps Motor 1 phase PWM A1/B1/C1 to GPIO 26/27/14, its enable to GPIO 21, and its current-sense inputs to GPIO 35/34. These are schematic facts; the first test still has to confirm enable polarity, phase/current polarity, scale, and powered behavior on the surviving channel.
- The motor supply is a Mean Well LRS-150-24. The installation's fuse/protection and immediate physical motor-power removal action remain to be recorded.
- The endstop switch is connected between GND and `I_0`; the board schematic maps `I_0` to GPIO 4. Its NO/NC contact choice and asserted logic level remain to be checked with the bridge disabled.
- The next stage is standalone unloaded-motor testing. The gearbox, belt, suction-box lift axis, and representative books are intentionally not part of this first powered stage and are not yet available for validation.

The `I_0` switch may be used as a fail-fast software stop/interlock during bench testing once its contact behavior is verified. It is not an emergency stop: it depends on the ESP32, input wiring, firmware, and bridge-disable path continuing to work. A separate operator-reachable action that physically removes motor power remains a prerequisite for energizing the motor.
