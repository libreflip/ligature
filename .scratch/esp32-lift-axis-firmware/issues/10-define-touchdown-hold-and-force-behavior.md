# Define touchdown, stationary hold, and press-level behavior

Type: grilling
Blocked by: 06, 07, 08

## Question

What dedicated `G30` control sequence should descend until genuine book contact, distinguish sustained velocity-sag/current saturation from startup acceleration or obstruction, cut the approach immediately at detection, establish and report the normalized 0–100% press level and stop Z, remain stationary across photo capture until explicit `M24`, and fail safely across representative book thicknesses and binding stiffnesses? Decide whether touchdown may cross the compiled bottom soft limit as proposed by `docs/plan/ligature.md`, define the separate absolute runaway bound if so, and specify the device measurements that choose and validate the maximum q-axis current represented by 100%, the default press level, and the hold behavior. Physical-force calibration is outside the current route.
