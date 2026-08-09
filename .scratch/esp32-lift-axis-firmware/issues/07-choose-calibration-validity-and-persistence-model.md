# Choose the calibration-validity and persistence model

Type: grilling
Status: resolved
Blocked by: 02, 03, 06

## Question

What versioned records must persist Ligature's electrical alignment (`M40`), sensor direction/electrical zero, current-sense phase mapping/gain signs, tuning profile, distance calibration (`M41`), force fit (`M42`), soft limits, and hardware/firmware identity; which boot-time values such as zero-current ADC offsets must be remeasured; how are compatibility and validity checked or invalidated; and what simplest fail-closed scheme blocks arming or ordinary motion without blocking explicit supervised commissioning or silently using guessed defaults?

## Answer

Use one source-controlled **commissioned configuration** compiled into the single firmware image. It contains an explicit operator-set `commissioned` marker and the accepted motor/pin mapping, sensor direction and electrical zero, current-sense phase mapping/gain signs/ampere scale, tuning and immutable limits, distance conversion, homing parameters, soft limits, maximum press current, and default press level. Calibration and hardware changes are an operator responsibility: edit the configuration, build, flash, home, and manually validate. Firmware does not identify hardware or manage NVS calibration records, versions, checksums, generations, or dependency fingerprints.

Ordinary arming requires the compiled commissioned marker, simple numeric sanity checks, and a successful zero-current ADC-offset measurement performed at every boot with PWM at zero. Check only required numeric presence, finiteness, positivity/order where applicable, and immutable safety ceilings; do not validate pins or hardware identity. Invalid configuration or offsets keep PWM at zero and refuse ordinary arming while leaving direct commissioning controls available. Homing remains separately required after every reset.

`M40`, `M41`, and SimpleFOC Studio changes produce candidate values in RAM and reports for the operator to copy into the source configuration. No pending calibration survives reset, and runtime changes never write flash. The one image exposes Studio directly without an unlock or exclusive commissioning mode; normal host commands may use runtime-modified values. A low-priority `runtime_modified` status flag may expose that condition. Reset restores the compiled configuration. Direct controls remain bounded by immutable electrical/motion ceilings and applicable soft limits; alignment and characterization stay explicit motion commands.

Drop `M42` physical-force fitting from this route. Use **press level**, a linear 0–100% mapping from zero to the commissioned maximum q-axis current, with a compiled default. It makes no physical-force claim; calibrated force can be added in a later effort if device needs justify it.

This deliberately favors simple, inspectable hackerspace controls over commercial-style access control. It supersedes the earlier commissioning decision's session/raw unlocks and JSON/NVS profile promotion while retaining its immutable ceilings, volatile tuning, explicit motion, and control-loop priority.
