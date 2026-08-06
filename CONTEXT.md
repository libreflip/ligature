# Bookscanner Lift Motor Control

Firmware concepts for controlling and validating the bookscanner's vertical suction-box mechanism.

## Language

**Lift axis**:
The assembled motor, gearbox, belt drive, endstop, and suction box whose vertical motion this firmware controls. Production validation includes this mechanism and representative books, not only an unloaded motor.
_Avoid_: Motor setup, Z axis

**Travel move**:
A commanded lift-axis move whose successful outcome requires reaching the requested position. An obstruction or inability to reach that position is a fault, not a successful partial move.
_Avoid_: Compliant move, torque move

**Touchdown**:
The deliberate downward contact-seeking operation that ends with the suction box stationary against the book at a controlled press level. It is the only normal motion where detecting resistance represents success.
_Avoid_: Move to zero, downward travel

**Commissioning interface**:
A development-only connection used during supervised bring-up to inspect telemetry and tune bounded controller parameters. It is not the production host-control protocol and must obey the firmware's motion-safety gates.
_Avoid_: Production control interface, maintenance API

**Commissioning session**:
An explicitly entered runtime state in which the commissioning interface may broadly control and tune the motor within immutable firmware safety ceilings and any optional narrower test limits. Opening or reconnecting the interface does not enter this state.
_Avoid_: GUI connection, ordinary arming

**Raw commissioning mode**:
A deliberate escalation within a commissioning session that permits otherwise-blocked SimpleFOC alignment, characterization, and autotune controls. It remains subject to immutable firmware ceilings and ends on explicit exit, stop, fault, or reset.
_Avoid_: Unrestricted mode, safety bypass

**Tuning profile**:
The versioned set of accepted controller parameters and limits promoted from volatile commissioning changes into a human-reviewable project file and ESP32 persistence. It excludes targets, enabled state, session state, and electrical-calibration identity.
_Avoid_: GUI state, calibration record

**Production host protocol**:
The ESP32 serial behavior contract used by the bookscanner host to arm, move, calibrate, query, and stop the lift axis. Its required semantics are stable independently of the particular command spellings used to express them.
_Avoid_: SimpleFOC GUI protocol, fixed G-code table

**Operation response**:
The production protocol's immediate acknowledgment that an operation was accepted, followed by exactly one terminal success or failure result. Requested status and unsolicited hard faults are separate from this lifecycle.
_Avoid_: Progress log, telemetry stream

**Diagnostic capture**:
A triggered, timestamped set of control-loop samples buffered on the ESP32 and transferred only on explicit request when transfer cannot disturb active control.
_Avoid_: Live serial logging, production telemetry stream
