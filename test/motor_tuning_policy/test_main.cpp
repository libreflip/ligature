#include <stdio.h>
#include <string.h>

#include "motor_tuning/command_policy.h"

namespace {

bool expect(const char* command, const TuningPolicyContext& context,
            const bool accepted, const bool write,
            const TuningCommandKind kind, const char* reason) {
  const TuningCommandDecision decision =
      evaluateTuningMotorCommand(command, context);
  if (decision.accepted != accepted || decision.write != write ||
      decision.kind != kind || strcmp(decision.reason, reason) != 0) {
    fprintf(stderr,
            "command %s: accepted=%d write=%d kind=%d reason=%s\n",
            command, decision.accepted, decision.write,
            static_cast<int>(decision.kind), decision.reason);
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const TuningPolicyContext motionless = {
      TuningSessionState::Motionless, TuningMotionMode::Velocity};
  const TuningPolicyContext alignedVelocity = {
      TuningSessionState::Aligned, TuningMotionMode::Velocity};
  const TuningPolicyContext armedVelocity = {
      TuningSessionState::Armed, TuningMotionMode::Velocity};
  const TuningPolicyContext armedTorque = {
      TuningSessionState::Armed, TuningMotionMode::Torque};
  const TuningPolicyContext armedAngle = {
      TuningSessionState::Armed, TuningMotionMode::Angle};
  const TuningPolicyContext armedOpenLoopVelocity = {
      TuningSessionState::Armed, TuningMotionMode::VelocityOpenLoop};
  const TuningPolicyContext armedOpenLoopAngle = {
      TuningSessionState::Armed, TuningMotionMode::AngleOpenLoop};

  if (!canClearTuningFault(true, true, true) ||
      canClearTuningFault(false, true, true) ||
      canClearTuningFault(true, false, true) ||
      canClearTuningFault(true, true, false)) {
    fprintf(stderr, "fault-clear precondition failed\n");
    return 1;
  }

  if (tuningMonitorFramingCommand("@3") !=
          TuningMonitorFraming::WebController ||
      tuningMonitorFramingCommand("@2") != TuningMonitorFraming::Studio ||
      tuningMonitorFramingCommand("MG6") !=
          TuningMonitorFraming::Unchanged) {
    fprintf(stderr, "client monitor framing failed\n");
    return 1;
  }

  if (tuningStateAfterEvent(TuningSessionState::Armed,
                            TuningSafetyEvent::Reset) !=
          TuningSessionState::Motionless ||
      tuningStateAfterEvent(TuningSessionState::Armed,
                            TuningSafetyEvent::Stop) !=
          TuningSessionState::Aligned ||
      tuningStateAfterEvent(TuningSessionState::Armed,
                            TuningSafetyEvent::SessionExit) !=
          TuningSessionState::Aligned ||
      tuningStateAfterEvent(TuningSessionState::Armed,
                            TuningSafetyEvent::ConnectionHandshake) !=
          TuningSessionState::Aligned ||
      tuningStateAfterEvent(TuningSessionState::Armed,
                            TuningSafetyEvent::Fault) !=
          TuningSessionState::Fault ||
      tuningStateAfterEvent(TuningSessionState::Motionless,
                            TuningSafetyEvent::Arm) !=
          TuningSessionState::Motionless) {
    fprintf(stderr, "safety-state transition failed\n");
    return 1;
  }

  if (!expect("", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("QP100000", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("QInan", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("SM-20990.488999999998", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("MD1", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("C6", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("T3", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("FR", motionless, false, false,
              TuningCommandKind::Passthrough, "USE_XALIGN") ||
      !expect("FC100", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("FP1", motionless, true, false,
              TuningCommandKind::Passthrough, "NONE") ||
      !expect("LU24", motionless, true, true,
              TuningCommandKind::LimitValue, "NONE") ||
      !expect("LU24.1", motionless, false, true,
              TuningCommandKind::LimitValue, "LIMIT_OUT_OF_BOUNDS") ||
      !expect("LC2.9", motionless, true, true,
              TuningCommandKind::LimitValue, "NONE") ||
      !expect("LC2.91", motionless, false, true,
              TuningCommandKind::LimitValue, "LIMIT_OUT_OF_BOUNDS") ||
      !expect("LV314.159", motionless, true, true,
              TuningCommandKind::LimitValue, "NONE") ||
      !expect("LV314.16", motionless, false, true,
              TuningCommandKind::LimitValue, "LIMIT_OUT_OF_BOUNDS") ||
      !expect("1.0", motionless, false, true,
              TuningCommandKind::Target, "ALIGNMENT_REQUIRED") ||
      !expect("314.159", alignedVelocity, true, true,
              TuningCommandKind::Target, "NONE") ||
      !expect("314.16", armedVelocity, false, true,
              TuningCommandKind::Target, "TARGET_OUT_OF_BOUNDS") ||
      !expect("100000", armedTorque, true, true,
              TuningCommandKind::Target, "NONE") ||
      !expect("100000", armedAngle, true, true,
              TuningCommandKind::Target, "NONE") ||
      !expect("314.159", armedOpenLoopVelocity, true, true,
              TuningCommandKind::Target, "NONE") ||
      !expect("100000", armedOpenLoopAngle, true, true,
              TuningCommandKind::Target, "NONE") ||
      !expect("E1", motionless, false, true,
              TuningCommandKind::MotorEnable, "ALIGNMENT_REQUIRED") ||
      !expect("E1", alignedVelocity, true, true,
              TuningCommandKind::MotorEnable, "NONE") ||
      !expect("E0", armedVelocity, true, true,
              TuningCommandKind::MotorEnable, "NONE")) {
    return 1;
  }

  puts("GREEN: WebController commands remain bounded and motion-gated");
  return 0;
}
