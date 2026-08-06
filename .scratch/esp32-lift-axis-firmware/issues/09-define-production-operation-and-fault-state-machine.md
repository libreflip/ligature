# Define the production operation and fault state machine

Type: grilling
Blocked by: 06, 07, 08, 10

## Question

What explicit Idle, commissioning/raw-commissioning, Armed, Homing, Moving, Probing, Turning, Holding, and Fault states and transitions implement Ligature's lifecycle without implicit motion or automatic resume? Specify one active operation, busy rejection, acknowledgment plus one terminal result, hold gating, calibration/homing gates, emergency `M112` preemption with disarm/Fault latch/homing invalidation and `M999` recovery, routine `M53` preemption that remains armed/homed, disconnect/reset behavior, and which status operations remain available in every state.
