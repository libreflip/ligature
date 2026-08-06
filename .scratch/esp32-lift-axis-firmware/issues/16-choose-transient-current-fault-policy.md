# Choose transient current fault policy

Type: grilling
Status: unclaimed

## Question

Given gearbox-attached operation where q-axis current typically remains near 0.7 A but brief measured-current peaks can exceed the 2.9 A symmetric ADC range and trigger an immediate gross-current fault, what current-command ceiling and transient overcurrent policy should commissioning and later production firmware use? Decide whether command limiting and fault detection need distinct thresholds or semantics, what duration/filtering or consecutive-sample evidence distinguishes current-loop/measurement ripple from sustained overcurrent, and which conditions—such as ADC clipping or non-finite feedback—must still stop immediately. Preserve the captured ADC-range evidence and require supervised device evidence before promoting the policy to production.
