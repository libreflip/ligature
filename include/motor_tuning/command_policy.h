#pragma once

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum class TuningSessionState {
  Motionless,
  Aligned,
  Armed,
  Fault,
};

enum class TuningMotionMode {
  Torque,
  Velocity,
  Angle,
  VelocityOpenLoop,
  AngleOpenLoop,
  AngleNoCascade,
  Custom,
};

enum class TuningSafetyEvent {
  Reset,
  AlignmentPassed,
  AlignmentFailed,
  Arm,
  Stop,
  SessionExit,
  ConnectionHandshake,
  Fault,
};

inline bool canClearTuningFault(const bool alignmentValid,
                                const bool hallStateLegal,
                                const bool motorReady) {
  return alignmentValid && hallStateLegal && motorReady;
}

inline TuningSessionState tuningStateAfterEvent(
    const TuningSessionState state, const TuningSafetyEvent event) {
  switch (event) {
    case TuningSafetyEvent::Reset:
      return TuningSessionState::Motionless;
    case TuningSafetyEvent::AlignmentPassed:
      return state == TuningSessionState::Motionless
                 ? TuningSessionState::Aligned
                 : state;
    case TuningSafetyEvent::AlignmentFailed:
    case TuningSafetyEvent::Fault:
      return TuningSessionState::Fault;
    case TuningSafetyEvent::Arm:
      return state == TuningSessionState::Aligned
                 ? TuningSessionState::Armed
                 : state;
    case TuningSafetyEvent::Stop:
    case TuningSafetyEvent::SessionExit:
    case TuningSafetyEvent::ConnectionHandshake:
      return state == TuningSessionState::Armed
                 ? TuningSessionState::Aligned
                 : state;
  }
  return state;
}

enum class TuningMonitorFraming {
  Unchanged,
  Studio,
  WebController,
};

enum class TuningCommandKind {
  Passthrough,
  Target,
  LimitValue,
  MotorEnable,
};

struct TuningCommandDecision {
  TuningCommandKind kind = TuningCommandKind::Passthrough;
  bool accepted = true;
  bool write = false;
  float value = 0.0F;
  const char* reason = "NONE";
};

struct TuningPolicyContext {
  TuningSessionState session = TuningSessionState::Motionless;
  TuningMotionMode motionMode = TuningMotionMode::Velocity;
};

constexpr float kTuningVoltageCeiling = 24.0F;
constexpr float kTuningCurrentCeiling = 2.9F;
constexpr float kTuningVelocityCeiling = 314.159265F;

inline size_t tuningCommandLength(const char* command) {
  size_t length = 0;
  while (command[length] != '\0' && command[length] != '\r' &&
         command[length] != '\n') {
    ++length;
  }
  return length;
}

inline TuningMonitorFraming tuningMonitorFramingCommand(
    const char* command) {
  const size_t length = tuningCommandLength(command);
  if (length != 2U || command[0] != '@') {
    return TuningMonitorFraming::Unchanged;
  }
  if (command[1] == '3') {
    return TuningMonitorFraming::WebController;
  }
  if (command[1] >= '0' && command[1] <= '2') {
    return TuningMonitorFraming::Studio;
  }
  return TuningMonitorFraming::Unchanged;
}

inline bool parseTuningFloat(const char* text, const size_t length,
                             float& value) {
  if (length == 0U || length >= 20U) {
    return false;
  }
  char buffer[20] = {};
  memcpy(buffer, text, length);
  char* end = nullptr;
  value = strtof(buffer, &end);
  return end == buffer + length && isfinite(value);
}

inline bool tuningCommandStartsWithNumber(const char value) {
  return value == '+' || value == '-' || value == '.' ||
         (value >= '0' && value <= '9');
}

inline TuningCommandDecision evaluateTuningMotorCommand(
    const char* command, const TuningPolicyContext& context) {
  const size_t length = tuningCommandLength(command);

  if (length == 2U && command[0] == 'F' && command[1] == 'R') {
    return {TuningCommandKind::Passthrough, false, false, 0.0F,
            "USE_XALIGN"};
  }

  if (command[0] == 'E' && length == 2U &&
      (command[1] == '0' || command[1] == '1')) {
    const bool enabling = command[1] == '1';
    const bool accepted = !enabling ||
                          context.session == TuningSessionState::Aligned ||
                          context.session == TuningSessionState::Armed;
    return {TuningCommandKind::MotorEnable, accepted, true,
            enabling ? 1.0F : 0.0F,
            accepted ? "NONE" : "ALIGNMENT_REQUIRED"};
  }

  float value = 0.0F;
  if (length > 2U && command[0] == 'L' &&
      strchr("UCV", command[1]) != nullptr) {
    if (!parseTuningFloat(command + 2U, length - 2U, value)) {
      return {TuningCommandKind::LimitValue, false, true, value,
              "NONFINITE_OR_INVALID"};
    }
    const float maximum = command[1] == 'U' ? kTuningVoltageCeiling
                          : command[1] == 'C' ? kTuningCurrentCeiling
                                              : kTuningVelocityCeiling;
    const bool accepted = value > 0.0F && value <= maximum;
    return {TuningCommandKind::LimitValue, accepted, true, value,
            accepted ? "NONE" : "LIMIT_OUT_OF_BOUNDS"};
  }

  if (length != 0U && tuningCommandStartsWithNumber(command[0])) {
    if (!parseTuningFloat(command, length, value)) {
      return {TuningCommandKind::Target, false, true, value,
              "NONFINITE_OR_INVALID"};
    }
    const bool authorized = context.session == TuningSessionState::Aligned ||
                            context.session == TuningSessionState::Armed;
    const bool velocityTarget =
        context.motionMode == TuningMotionMode::Velocity ||
        context.motionMode == TuningMotionMode::VelocityOpenLoop;
    const bool withinLimit = !velocityTarget ||
                             fabsf(value) <= kTuningVelocityCeiling;
    return {TuningCommandKind::Target, authorized && withinLimit, true, value,
            !authorized ? "ALIGNMENT_REQUIRED"
                        : (withinLimit ? "NONE" : "TARGET_OUT_OF_BOUNDS")};
  }

  return {};
}
