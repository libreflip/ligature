# Define the production protocol and observability contract

Type: grilling
Blocked by: 05, 09, 10

## Question

What exact 115200-baud line contract should Ligature expose for `M3`/`M5`, `M112`/`M999`, routine `M53`, the one-command positional-limit override and its relative move, `G28`, `G0`/`G1`, `G30`/`M24`, `M40`/`M41`, `?`, and `M155`? Decide whether firmware should expose a composite `G73` page-turn command and `Turning` state at all or leave every phase as a Raspberry-Pi-sequenced ordinary move; if retained, resolve the proposed `G73 U/R/B/C/Z` shape and blower events. Resolve the draft's internal contradiction around reserving `S` while using `M155 S`. Define structural arm-command unambiguity, parsing/validation and soft-limit errors, single-terminal preemption lines that identify the cancelled command, state-derived position-trust reporting without a separate `Homed` boolean, status and `status` heartbeat fields/rates, unsolicited faults, bounded diagnostic-capture transfer, and host-side conformance tests without blocking active control.
