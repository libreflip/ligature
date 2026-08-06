# Choose the control architecture and travel-fault criteria

Type: grilling
Blocked by: 02, 04, 05, 14

## Question

Which SimpleFOC current, velocity, and position-loop structure should implement Ligature's normal position travel, given the coarse Hall velocity evidence from the unloaded smoke test; how should model-based limiting protect first alignment before measured `foc_current` control is trusted; and what current, voltage, velocity, acceleration/output-ramp, thermal, target-tolerance, timeout, saturation, tracking-error, and obstruction criteria make a travel move succeed versus fail and disarm? Keep generalized modal torque travel out of scope: touchdown is the only production contact-seeking move.
