#pragma once

#include "production/controller.h"

namespace ligature {

// Deliberately fail closed. Set commissioned=true only after the assembled lift
// axis has supplied every placeholder below and the resulting image has passed
// the supervised commissioning procedure. Geared-motor tuning evidence is not
// an assembled lift-axis commissioned configuration.
constexpr CommissionedConfig kCommissionedConfig = {
    false,   // commissioned
    0.0F,    // millimetresPerRadian
    -200.0F, // bottomLimitMm (placeholder; never used while uncommissioned)
    -2.0F,   // topLimitMm
    0.2F,    // positionToleranceMm
    20.0F,   // fastSpeedMmS
    5.0F,    // slowSpeedMmS -- provisional low-speed strategy seam
    10.0F,   // homingSpeedMmS
    3.0F,    // pullOffSpeedMmS
    1.5F,    // currentLimitA
    2.9F,    // maximumCurrentA -- provisional transient-current policy seam
    3.0F,    // voltageLimitV
    24.0F,   // maximumVoltageV
    50.0F,   // maximumSpeedMmS
    60.0F,   // maximumOperationSeconds
    2.0F,    // startupAllowanceSeconds
    0.5F,    // settleSeconds
    1.0F,    // progressDwellSeconds
    1.0F,    // touchdownGraceSeconds
    0.5F,    // touchdownDwellSeconds
    0.2F,    // touchdownCurrentToleranceA
    1.5F,    // maximumPressCurrentA
    50.0F,   // defaultPressPercent
};

}  // namespace ligature
