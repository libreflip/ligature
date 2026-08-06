# Choose low-speed geared-motor feedback strategy

Type: grilling
Status: unclaimed

## Question

Given a valid 1.5 V gearbox-attached alignment and final-firmware device evidence that stock Hall-based closed-loop velocity physically pauses and jumps at a 10 rad/s target—velocity cycling from roughly 0 to 30 rad/s despite an average near target—while operation becomes usable around 20 rad/s and open-loop low-speed motion is materially smoother, what feedback/control strategy should support low-speed geared-motor motion? Decide whether continuous 10 rad/s operation is actually required by later lift-axis behavior; whether to change Hall velocity estimation, add a bounded breakaway/feed-forward policy, tune the cascade around a minimum usable velocity, or require different position sensing; and what device experiment distinguishes these options. Preserve the existing rejection of the prior Hall-interpolation prototype and do not relax Task 15's criterion without an explicit decision.
