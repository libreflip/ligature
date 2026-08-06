# Define the production protocol and observability contract

Type: grilling
Blocked by: 05, 09, 10

## Question

What exact 115200-baud line contract should Ligature expose for `M3`/`M5`, `M112`/`M999`, routine `M53`, `G28`, `G0`/`G1`, `G30`/`M24`, `G73`, `M40`–`M42`, `?`, and `M155`? Resolve the proposed `G73 U/R/B/C/Z` shape and the draft's internal contradictions around reserving `S` while using `M155 S`, and around move-to-top being available immediately after an `M112` that latches Fault and invalidates homing. Define structural arm-command unambiguity, parsing/validation and soft-limit errors, acknowledgment plus exactly one terminal result, interrupted-operation response ownership, status and `status` heartbeat fields/rates, `G73` blower events, unsolicited faults, bounded diagnostic-capture transfer, and host-side conformance tests without blocking active control.
