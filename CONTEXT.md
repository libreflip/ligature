# Bookscanner Lift Motor Control

Firmware concepts for controlling and validating the bookscanner's vertical suction-box mechanism.

## Language

**Lift axis**:
The assembled motor, gearbox, belt drive, endstop, and suction box whose vertical motion this firmware controls. Production validation includes this mechanism and representative books, not only an unloaded motor.
_Avoid_: Motor setup, Z axis

**Geared motor assembly**:
Motor 1 with its 10:1 gearbox attached while the gearbox output is otherwise unloaded. It is a commissioning test boundary, not the assembled lift axis and not production-load validation.
_Avoid_: Lift axis, unloaded motor

**Travel move**:
A commanded lift-axis move whose successful outcome requires reaching the requested position. An obstruction or inability to reach that position is a fault, not a successful partial move.
_Avoid_: Compliant move, torque move

**Touchdown**:
The deliberate downward contact-seeking operation that ends with the suction box stationary against the book at a controlled press level. It is the only normal motion where detecting resistance represents success.
_Avoid_: Move to zero, downward travel

**Commissioning interface**:
The direct SimpleFOC Studio connection used during supervised bring-up to inspect telemetry and tune runtime controller parameters within immutable firmware safety ceilings. It requires no unlock or exclusive mode and is distinct from the production host protocol.
_Avoid_: Commissioning session, production control interface, maintenance API

**Tuning profile**:
The accepted controller parameters and limits compiled into a commissioned configuration after supervised tuning. Runtime commissioning changes may override it until reset but never modify it.
_Avoid_: GUI state, calibration record

**Commissioned configuration**:
The manually validated lift-axis calibration, tuning, limits, and hardware settings compiled into a firmware image together with an explicit commissioned marker. It is immutable at runtime and restored on reset.
_Avoid_: NVS calibration, automatic hardware identity

**Press level**:
A normalized 0–100% touchdown and hold command mapped linearly to the commissioned maximum q-axis current. It is not a calibrated physical force.
_Avoid_: Force, pressure calibration

**Production host protocol**:
The ESP32 serial behavior contract used by the bookscanner host to arm, move, calibrate, query, and stop the lift axis. Its required semantics are stable independently of the particular command spellings used to express them.
_Avoid_: SimpleFOC GUI protocol, fixed G-code table

**Operation response**:
The production protocol's immediate acknowledgment that an operation was accepted, followed by exactly one terminal success or failure result. Requested status and unsolicited hard faults are separate from this lifecycle.
_Avoid_: Progress log, telemetry stream

**Diagnostic capture**:
A triggered, timestamped set of control-loop samples buffered on the ESP32 and transferred only on explicit request when transfer cannot disturb active control.
_Avoid_: Live serial logging, production telemetry stream

**Lift-axis coordinate frame**:
The homed position at the upper endstop is Z=0. Upward motion increases Z and downward motion decreases Z, so positions within the ordinary travel range are negative.
_Avoid_: Positive-downward Z, depth as positive Z

**Positional-limit override**:
A one-command authorization for the next relative travel move to proceed without trusted homing or positional soft-limit checks. It does not override the endstop rule or electrical and motion safety ceilings.
_Avoid_: Safety override, unlimited jog, unhomed absolute move
