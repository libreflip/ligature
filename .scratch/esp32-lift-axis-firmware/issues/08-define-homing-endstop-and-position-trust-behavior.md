# Define homing, endstop, and position-trust behavior

Type: grilling
Blocked by: 07, 13

## Question

How should Ligature implement and validate its fixed top-endstop seek/back-off/slow-locate homing sequence, positive-downward Z frame, persisted top/bottom soft limits, and immediate `ENDSTOP_UNEXPECTED` fault outside homing? Define switch filtering, seek/pull-off/locate bounds, command rejection at each soft limit, the allowed homing exception, and position-trust invalidation. Emergency stop (`M112`), reset, and hard faults must clear homing; routine abort (`M53`) is intended to preserve it, so what repeated assembled-axis stop-position experiment and error bound are required before that preservation is accepted?
