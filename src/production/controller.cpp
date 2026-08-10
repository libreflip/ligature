#include "production/controller.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ligature {
namespace {

constexpr float kPi = 3.14159265359F;
constexpr float kTwoPi = 2.0F * kPi;

bool elapsed(const uint32_t now, const uint32_t start,
             const uint32_t duration) {
  return static_cast<uint32_t>(now - start) >= duration;
}

bool isUpper(const char value) { return value >= 'A' && value <= 'Z'; }
bool isDigit(const char value) { return value >= '0' && value <= '9'; }

}  // namespace

Controller::Controller(const CommissionedConfig& config, LineSink& output)
    : config_(config),
      output_(output),
      state_(State::CommissioningOnly),
      homingPhase_(HomingPhase::Seek),
      drive_{DriveMode::PwmOff, 0.0F, 0.0F, 0.0F},
      homeAngleRad_(0.0F),
      operationStartMm_(0.0F),
      targetMm_(0.0F),
      pressPercent_(0.0F),
      calibrationRadians_(0.0F),
      operationStartedMs_(0U),
      deadlineMs_(0U),
      lastProgressMs_(0U),
      settleStartedMs_(0U),
      saturationStartedMs_(0U),
      touchdownContactDetected_(false),
      heartbeatIntervalMs_(0U),
      nextHeartbeatMs_(0U),
      activeToken_{},
      fault_{},
      capture_{},
      captureState_(CaptureState::Empty),
      captureLimit_(0U),
      captureCount_(0U),
      captureDecimation_(1U),
      captureTicks_(0U) {}

bool Controller::configurationValid() const {
  return config_.commissioned && isfinite(config_.millimetresPerRadian) &&
         config_.millimetresPerRadian > 0.0F &&
         isfinite(config_.bottomLimitMm) && isfinite(config_.topLimitMm) &&
         config_.bottomLimitMm < config_.topLimitMm &&
         config_.topLimitMm == -2.0F && config_.positionToleranceMm > 0.0F &&
         config_.fastSpeedMmS > 0.0F && config_.slowSpeedMmS > 0.0F &&
         config_.homingSpeedMmS > 0.0F && config_.pullOffSpeedMmS > 0.0F &&
         config_.currentLimitA > 0.0F &&
         config_.currentLimitA <= config_.maximumCurrentA &&
         config_.voltageLimitV > 0.0F &&
         config_.voltageLimitV <= config_.maximumVoltageV &&
         config_.maximumSpeedMmS > 0.0F &&
         config_.maximumOperationSeconds > 0.0F &&
         config_.maximumPressCurrentA > 0.0F &&
         config_.maximumPressCurrentA <= config_.maximumCurrentA &&
         config_.defaultPressPercent >= 0.0F &&
         config_.defaultPressPercent <= 100.0F;
}

void Controller::reset(const Observation& observation,
                       const bool adcOffsetsValid) {
  commandPwmOff();
  heartbeatIntervalMs_ = 0U;
  nextHeartbeatMs_ = 0U;
  captureState_ = CaptureState::Empty;
  captureLimit_ = 0U;
  captureCount_ = 0U;
  captureTicks_ = 0U;
  homeAngleRad_ = observation.shaftAngleRad;
  fault_[0] = '\0';
  state_ = configurationValid() && adcOffsetsValid
               ? State::IdleUntrusted
               : State::CommissioningOnly;
}

bool Controller::positionTrusted() const {
  switch (state_) {
    case State::IdleTrusted:
    case State::Ready:
    case State::AligningConfigured:  // Trust is invalidated when it completes.
    case State::Calibrating:
    case State::Moving:
    case State::TouchingDown:
    case State::Holding:
    case State::FaultTrusted:
      return state_ != State::AligningConfigured;
    default:
      return false;
  }
}

bool Controller::exclusiveOperationActive() const {
  switch (state_) {
    case State::AligningCommissioningOnly:
    case State::AligningConfigured:
    case State::Homing:
    case State::Calibrating:
    case State::Moving:
    case State::MovingUntrusted:
    case State::TouchingDown:
      return true;
    default:
      return false;
  }
}

bool Controller::parseLine(const char* line, const size_t length,
                           ParsedLine& parsed, const char*& reason) const {
  parsed = {};
  if (length == 0U) {
    reason = "SYNTAX";
    return false;
  }
  if (length > kMaximumProductionLine) {
    reason = "LINE_TOO_LONG";
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(line[index]);
    if (value < 0x20U || value > 0x7eU) {
      reason = "NON_ASCII";
      return false;
    }
  }

  size_t position = 0U;
  size_t commandLength = 0U;
  if (line[0] == '?') {
    commandLength = 1U;
    position = 1U;
  } else {
    while (position < length && line[position] != ' ') {
      if (commandLength >= sizeof(parsed.command) - 1U ||
          !(isUpper(line[position]) || isDigit(line[position]))) {
        reason = "SYNTAX";
        return false;
      }
      parsed.command[commandLength++] = line[position++];
    }
  }
  if (commandLength == 0U) {
    reason = "SYNTAX";
    return false;
  }
  parsed.command[commandLength] = '\0';

  while (position < length) {
    if (line[position] != ' ') {
      reason = "SYNTAX";
      return false;
    }
    while (position < length && line[position] == ' ') {
      ++position;
    }
    if (position == length || !isUpper(line[position]) ||
        parsed.parameterCount >= 3U) {
      reason = "SYNTAX";
      return false;
    }
    const char letter = line[position++];
    for (uint8_t index = 0; index < parsed.parameterCount; ++index) {
      if (parsed.parameters[index].letter == letter) {
        reason = "DUPLICATE_PARAM";
        return false;
      }
    }
    const size_t numberStart = position;
    if (position < length && (line[position] == '+' || line[position] == '-')) {
      ++position;
    }
    bool digits = false;
    while (position < length && isDigit(line[position])) {
      digits = true;
      ++position;
    }
    if (position < length && line[position] == '.') {
      ++position;
      while (position < length && isDigit(line[position])) {
        digits = true;
        ++position;
      }
    }
    if (!digits || (position < length && line[position] != ' ')) {
      reason = "INVALID_PARAM";
      return false;
    }
    char number[24] = {};
    const size_t numberLength = position - numberStart;
    if (numberLength >= sizeof(number)) {
      reason = "INVALID_PARAM";
      return false;
    }
    memcpy(number, line + numberStart, numberLength);
    char* end = nullptr;
    const float value = strtof(number, &end);
    if (end != number + numberLength || !isfinite(value)) {
      reason = "INVALID_PARAM";
      return false;
    }
    parsed.parameters[parsed.parameterCount++] = {letter, value};
  }
  return true;
}

const Controller::Parameter* Controller::findParameter(
    const ParsedLine& parsed, const char letter) const {
  for (uint8_t index = 0; index < parsed.parameterCount; ++index) {
    if (parsed.parameters[index].letter == letter) {
      return &parsed.parameters[index];
    }
  }
  return nullptr;
}

bool Controller::requireNoParameters(const ParsedLine& parsed) {
  if (parsed.parameterCount == 0U) {
    return true;
  }
  emitError(parsed.command, "INVALID_PARAM");
  return false;
}

bool Controller::requireSchema(const ParsedLine& parsed, const char* allowed,
                               const char required,
                               const char*& reason) const {
  for (uint8_t index = 0; index < parsed.parameterCount; ++index) {
    if (strchr(allowed, parsed.parameters[index].letter) == nullptr) {
      reason = "INVALID_PARAM";
      return false;
    }
  }
  if (required != '\0' && findParameter(parsed, required) == nullptr) {
    reason = "MISSING_PARAM";
    return false;
  }
  return true;
}

void Controller::acceptLine(const char* line, size_t length,
                            const Observation& observation) {
  if (length != 0U && line[length - 1U] == '\r') {
    --length;
  }
  ParsedLine parsed;
  const char* reason = nullptr;
  if (!parseLine(line, length, parsed, reason)) {
    emitError("LINE", reason);
    return;
  }
  dispatch(parsed, observation);
}

void Controller::dispatch(const ParsedLine& parsed,
                          const Observation& observation) {
  const bool faulted = state_ == State::FaultTrusted ||
                       state_ == State::FaultUntrusted;
  const bool immediateAlways = strcmp(parsed.command, "?") == 0 ||
                               strcmp(parsed.command, "M155") == 0 ||
                               strcmp(parsed.command, "M112") == 0 ||
                               strcmp(parsed.command, "M53") == 0;
  if (exclusiveOperationActive() && !immediateAlways) {
    emitError(parsed.command, "BUSY");
    return;
  }
  if (faulted && strcmp(parsed.command, "?") != 0 &&
      strcmp(parsed.command, "M155") != 0 &&
      strcmp(parsed.command, "M999") != 0 &&
      strcmp(parsed.command, "M112") != 0) {
    emitError(parsed.command, "FAULTED");
    return;
  }

  if (strcmp(parsed.command, "?") == 0) {
    if (requireNoParameters(parsed)) {
      emitStatus(observation, "state");
    }
    return;
  }
  if (strcmp(parsed.command, "M155") == 0) {
    const char* reason = nullptr;
    if (!requireSchema(parsed, "S", 'S', reason)) {
      emitError("M155", reason);
      return;
    }
    const float seconds = findParameter(parsed, 'S')->value;
    if (seconds != 0.0F && (seconds < 0.2F || seconds > 60.0F)) {
      emitError("M155", "OUT_OF_RANGE");
      return;
    }
    heartbeatIntervalMs_ = static_cast<uint32_t>(seconds * 1000.0F + 0.5F);
    nextHeartbeatMs_ = observation.nowMs + heartbeatIntervalMs_;
    char fields[32];
    snprintf(fields, sizeof(fields), "S:%.3f", seconds);
    emitDone("M155", fields);
    return;
  }
  if (strcmp(parsed.command, "M120") == 0) {
    const char* reason = nullptr;
    if (!requireSchema(parsed, "ND", 'N', reason)) {
      emitError("M120", reason);
      return;
    }
    if (exclusiveOperationActive() || captureState_ != CaptureState::Empty) {
      emitError("M120", "BUSY");
      return;
    }
    const float samples = findParameter(parsed, 'N')->value;
    const Parameter* decimationParameter = findParameter(parsed, 'D');
    const float decimation = decimationParameter == nullptr
                                 ? 1.0F
                                 : decimationParameter->value;
    if (samples < 1.0F || samples > 64.0F || samples != floorf(samples) ||
        decimation < 1.0F || decimation > 1000.0F ||
        decimation != floorf(decimation)) {
      emitError("M120", "OUT_OF_RANGE");
      return;
    }
    captureLimit_ = static_cast<uint16_t>(samples);
    captureDecimation_ = static_cast<uint16_t>(decimation);
    captureCount_ = 0U;
    captureTicks_ = 0U;
    captureState_ = CaptureState::Armed;
    emitDone("M120", "STATE:ARMED");
    return;
  }
  if (strcmp(parsed.command, "M121") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (captureState_ != CaptureState::Complete ||
        drive_.mode != DriveMode::PwmOff || exclusiveOperationActive()) {
      emitError("M121", "BUSY");
      return;
    }
    transferCapture();
    return;
  }
  if (strcmp(parsed.command, "M122") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (exclusiveOperationActive()) {
      emitError("M122", "BUSY");
      return;
    }
    captureState_ = CaptureState::Empty;
    captureCount_ = 0U;
    emitDone("M122");
    return;
  }
  if (strcmp(parsed.command, "M112") == 0) {
    if (requireNoParameters(parsed)) {
      cancel("M112", true, observation);
    }
    return;
  }
  if (strcmp(parsed.command, "M53") == 0) {
    if (requireNoParameters(parsed)) {
      cancel("M53", false, observation);
    }
    return;
  }
  if (strcmp(parsed.command, "M999") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ == State::FaultTrusted) enterIdle(true);
    if (state_ == State::FaultUntrusted) enterIdle(false);
    fault_[0] = '\0';
    emitDone("M999", semanticState());
    return;
  }
  if (strcmp(parsed.command, "M3") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ == State::CommissioningOnly) {
      emitError("M3", "CONFIG_INVALID");
    } else if (state_ == State::IdleUntrusted) {
      state_ = State::ArmedUntrusted;
      commandHold(observation);
      emitDone("M3", "STATE:ARMED TRUST:0");
    } else if (state_ == State::IdleTrusted) {
      state_ = State::Ready;
      commandHold(observation);
      emitDone("M3", "STATE:READY TRUST:1");
    } else if (state_ == State::ArmedUntrusted || state_ == State::Ready ||
               state_ == State::Holding) {
      emitDone("M3", positionTrusted() ? "STATE:READY TRUST:1"
                                       : "STATE:ARMED TRUST:0");
    } else {
      emitError("M3", "NOT_ARMED");
    }
    return;
  }
  if (strcmp(parsed.command, "M5") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ == State::IdleUntrusted || state_ == State::IdleTrusted) {
      commandPwmOff();
    } else if (state_ == State::ArmedUntrusted ||
               state_ == State::ArmedOverridePending) {
      enterIdle(false);
    } else if (state_ == State::Ready || state_ == State::Holding) {
      enterIdle(true);
    } else {
      emitError("M5", "BUSY");
      return;
    }
    emitDone("M5", positionTrusted() ? "STATE:IDLE TRUST:1"
                                     : "STATE:IDLE TRUST:0");
    return;
  }
  if (strcmp(parsed.command, "M52") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ != State::ArmedUntrusted &&
        state_ != State::ArmedOverridePending) {
      emitError("M52", state_ == State::Ready ? "OVERRIDE_REQUIRED"
                                               : "NOT_ARMED");
      return;
    }
    state_ = State::ArmedOverridePending;
    emitDone("M52", "STATE:OVERRIDE_PENDING TRUST:0");
    return;
  }
  if (strcmp(parsed.command, "G28") == 0) {
    if (!requireNoParameters(parsed)) return;
    startHoming(observation);
    return;
  }
  if (strcmp(parsed.command, "G0") == 0 ||
      strcmp(parsed.command, "G1") == 0) {
    const bool relative = findParameter(parsed, 'D') != nullptr;
    startTravel(parsed, observation, relative);
    return;
  }
  if (strcmp(parsed.command, "G30") == 0) {
    startTouchdown(parsed, observation);
    return;
  }
  if (strcmp(parsed.command, "M24") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ != State::Holding) {
      emitError("M24", "NOT_HOLDING");
      return;
    }
    commandHold(observation);
    state_ = State::Ready;
    char fields[80];
    snprintf(fields, sizeof(fields), "Z:%.3f STATE:READY TRUST:1",
             positionMm(observation));
    emitDone("M24", fields);
    return;
  }
  if (strcmp(parsed.command, "M40") == 0) {
    if (!requireNoParameters(parsed)) return;
    if (state_ != State::CommissioningOnly &&
        state_ != State::IdleUntrusted && state_ != State::IdleTrusted) {
      emitError("M40", "BUSY");
      return;
    }
    state_ = state_ == State::CommissioningOnly
                 ? State::AligningCommissioningOnly
                 : State::AligningConfigured;
    strcpy(activeToken_, "M40");
    drive_ = {DriveMode::Alignment, 0.0F, 0.0F, config_.maximumCurrentA};
    emit("ok M40");
    return;
  }
  if (strcmp(parsed.command, "M41") == 0) {
    const Parameter* revolutions = findParameter(parsed, 'R');
    const Parameter* measured = findParameter(parsed, 'Z');
    const char* reason = nullptr;
    if (!requireSchema(parsed, "RZ", '\0', reason) ||
        (revolutions == nullptr) == (measured == nullptr)) {
      emitError("M41", reason == nullptr ? "INVALID_PARAM" : reason);
      return;
    }
    if (measured != nullptr) {
      if (calibrationRadians_ == 0.0F || measured->value == 0.0F) {
        emitError("M41", "CALIBRATION_FAILED");
        return;
      }
      const float mmPerRev = measured->value /
                             (calibrationRadians_ / kTwoPi);
      char fields[64];
      snprintf(fields, sizeof(fields), "MM_PER_REV:%.6f VOLATILE:1",
               mmPerRev);
      calibrationRadians_ = 0.0F;
      emitDone("M41", fields);
      return;
    }
    if (state_ != State::Ready) {
      emitError("M41", state_ == State::ArmedUntrusted
                           ? "POSITION_UNTRUSTED"
                           : "NOT_ARMED");
      return;
    }
    ParsedLine travel = parsed;
    strcpy(travel.command, "M41");
    calibrationRadians_ = revolutions->value * kTwoPi;
    state_ = State::Calibrating;
    strcpy(activeToken_, "M41");
    targetMm_ = positionMm(observation) +
                calibrationRadians_ * config_.millimetresPerRadian;
    operationStartMm_ = positionMm(observation);
    const float calibrationDuration =
        fabsf(targetMm_ - operationStartMm_) / config_.slowSpeedMmS +
        config_.startupAllowanceSeconds + config_.settleSeconds;
    if (targetMm_ < config_.bottomLimitMm ||
        targetMm_ > config_.topLimitMm ||
        calibrationDuration > config_.maximumOperationSeconds) {
      state_ = State::Ready;
      calibrationRadians_ = 0.0F;
      emitError("M41", targetMm_ < config_.bottomLimitMm ||
                           targetMm_ > config_.topLimitMm
                       ? "SOFT_LIMIT"
                       : "OUT_OF_RANGE");
      return;
    }
    operationStartedMs_ = observation.nowMs;
    deadlineMs_ = secondsToDeadline(calibrationDuration, observation);
    drive_ = {DriveMode::Position,
              observation.shaftAngleRad + calibrationRadians_,
              config_.slowSpeedMmS / config_.millimetresPerRadian,
              config_.currentLimitA};
    emit("ok M41");
    return;
  }

  // M120-M122 and native Studio adaptation are adapter-level baseline seams;
  // unknown production-looking lines must never fall through to Commander.
  emitError(parsed.command, "UNKNOWN_COMMAND");
}

void Controller::startTravel(const ParsedLine& parsed,
                             const Observation& observation,
                             const bool relative) {
  const char* reason = nullptr;
  if (!requireSchema(parsed, relative ? "DF" : "ZF",
                     relative ? 'D' : 'Z', reason)) {
    emitError(parsed.command, reason);
    return;
  }
  if (relative) {
    if (strcmp(parsed.command, "G1") != 0 ||
        state_ != State::ArmedOverridePending) {
      emitError(parsed.command, "OVERRIDE_REQUIRED");
      return;
    }
  } else if (state_ != State::Ready) {
    emitError(parsed.command, state_ == State::ArmedUntrusted ||
                                      state_ == State::ArmedOverridePending
                                  ? "POSITION_UNTRUSTED"
                                  : "NOT_ARMED");
    return;
  }

  const float start = relative ? 0.0F : positionMm(observation);
  const float target = relative ? findParameter(parsed, 'D')->value
                                : findParameter(parsed, 'Z')->value;
  const Parameter* feed = findParameter(parsed, 'F');
  float speed = strcmp(parsed.command, "G0") == 0 ? config_.fastSpeedMmS
                                                   : config_.slowSpeedMmS;
  if (feed != nullptr) speed = feed->value / 60.0F;
  if (speed <= 0.0F || speed > config_.maximumSpeedMmS) {
    emitError(parsed.command, "OUT_OF_RANGE");
    return;
  }
  if (!relative && (target < config_.bottomLimitMm ||
                    target > config_.topLimitMm)) {
    emit("error %s REASON:SOFT_LIMIT TARGET:%.3f MIN:%.3f MAX:%.3f",
         parsed.command, target, config_.bottomLimitMm, config_.topLimitMm);
    return;
  }
  const float distance = fabsf(target - start);
  const float duration = distance / speed + config_.startupAllowanceSeconds +
                         config_.settleSeconds;
  if (duration > config_.maximumOperationSeconds) {
    emitError(parsed.command, "OUT_OF_RANGE");
    return;
  }

  strcpy(activeToken_, parsed.command);
  state_ = relative ? State::MovingUntrusted : State::Moving;
  operationStartMm_ = start;
  targetMm_ = target;
  operationStartedMs_ = observation.nowMs;
  lastProgressMs_ = observation.nowMs;
  settleStartedMs_ = 0U;
  deadlineMs_ = secondsToDeadline(duration, observation);
  const float targetAngle = relative
      ? observation.shaftAngleRad + target / config_.millimetresPerRadian
      : homeAngleRad_ + target / config_.millimetresPerRadian;
  drive_ = {DriveMode::Position, targetAngle,
            provisionalLowSpeedRequest(speed) /
                config_.millimetresPerRadian,
            config_.currentLimitA};
  emit("ok %s", parsed.command);
}

void Controller::startHoming(const Observation& observation) {
  if (state_ != State::ArmedUntrusted && state_ != State::Ready) {
    emitError("G28", "NOT_ARMED");
    return;
  }
  state_ = State::Homing;
  strcpy(activeToken_, "G28");
  homingPhase_ = observation.endstopActive ? HomingPhase::Release
                                           : HomingPhase::Seek;
  operationStartMm_ = observation.shaftAngleRad;
  operationStartedMs_ = observation.nowMs;
  lastProgressMs_ = observation.nowMs;
  const float maximum = config_.maximumOperationSeconds;
  deadlineMs_ = secondsToDeadline(maximum, observation);
  const float velocity = homingPhase_ == HomingPhase::Seek
                             ? config_.homingSpeedMmS
                             : -config_.pullOffSpeedMmS;
  drive_ = {DriveMode::Velocity,
            velocity / config_.millimetresPerRadian,
            fabsf(velocity / config_.millimetresPerRadian),
            config_.currentLimitA};
  emit("ok G28");
}

void Controller::startTouchdown(const ParsedLine& parsed,
                                const Observation& observation) {
  const char* reason = nullptr;
  if (!requireSchema(parsed, "P", '\0', reason)) {
    emitError("G30", reason);
    return;
  }
  if (state_ != State::Ready) {
    emitError("G30", state_ == State::ArmedUntrusted
                         ? "POSITION_UNTRUSTED"
                         : "NOT_ARMED");
    return;
  }
  const float currentZ = positionMm(observation);
  if (currentZ <= config_.bottomLimitMm) {
    emitError("G30", "SOFT_LIMIT");
    return;
  }
  const Parameter* press = findParameter(parsed, 'P');
  pressPercent_ = press == nullptr ? config_.defaultPressPercent
                                  : press->value;
  if (pressPercent_ < 0.0F || pressPercent_ > 100.0F) {
    emitError("G30", "OUT_OF_RANGE");
    return;
  }
  state_ = State::TouchingDown;
  strcpy(activeToken_, "G30");
  operationStartMm_ = currentZ;
  targetMm_ = config_.bottomLimitMm;
  operationStartedMs_ = observation.nowMs;
  lastProgressMs_ = observation.nowMs;
  saturationStartedMs_ = 0U;
  touchdownContactDetected_ = false;
  const float seconds = (currentZ - config_.bottomLimitMm) /
                            config_.slowSpeedMmS +
                        config_.startupAllowanceSeconds;
  deadlineMs_ = secondsToDeadline(seconds, observation);
  const float current = config_.maximumPressCurrentA * pressPercent_ / 100.0F;
  drive_ = {DriveMode::Velocity,
            -provisionalLowSpeedRequest(config_.slowSpeedMmS) /
                config_.millimetresPerRadian,
            provisionalLowSpeedRequest(config_.slowSpeedMmS) /
                config_.millimetresPerRadian,
            current};
  emit("ok G30");
}

void Controller::cancel(const char* command, const bool emergency,
                        const Observation& observation) {
  const bool active = exclusiveOperationActive() || state_ == State::Holding ||
                      state_ == State::Ready;
  if (!emergency && !active) {
    emitError(command, "NO_ACTIVE_OPERATION");
    return;
  }
  char cancelled[8];
  strcpy(cancelled, exclusiveOperationActive() ? activeCommand() : "NONE");
  const bool trusted = positionTrusted() || state_ == State::Moving ||
                       state_ == State::TouchingDown ||
                       state_ == State::Holding || state_ == State::Ready;
  commandPwmOff();
  if (captureState_ == CaptureState::Recording) {
    captureState_ = CaptureState::Complete;
  }
  if (emergency) {
    enterFault(trusted && state_ != State::Homing);
  } else if (state_ == State::AligningCommissioningOnly) {
    state_ = State::CommissioningOnly;
  } else if (state_ == State::AligningConfigured) {
    state_ = State::IdleUntrusted;
  } else if (state_ == State::Homing || state_ == State::MovingUntrusted) {
    state_ = State::ArmedUntrusted;
    commandHold(observation);
  } else {
    state_ = State::Ready;
    commandHold(observation);
  }
  char fields[112];
  snprintf(fields, sizeof(fields), "CANCELLED:%s Z:%s STATE:%s TRUST:%d",
           cancelled, positionTrusted() ? "KNOWN" : "?", semanticState(),
           positionTrusted() ? 1 : 0);
  emitDone(command, fields);
}

void Controller::alignmentComplete(const bool passed,
                                   const float electricalZero,
                                   const char* sensorDirection,
                                   const Observation& observation) {
  if (state_ != State::AligningCommissioningOnly &&
      state_ != State::AligningConfigured) {
    return;
  }
  const bool commissioningOnly = state_ == State::AligningCommissioningOnly;
  commandPwmOff();
  if (!passed || !isfinite(electricalZero)) {
    state_ = commissioningOnly ? State::CommissioningOnly
                               : State::IdleUntrusted;
    emitError("M40", "ALIGNMENT_FAILED");
    return;
  }
  state_ = commissioningOnly ? State::CommissioningOnly
                             : State::IdleUntrusted;
  homeAngleRad_ = observation.shaftAngleRad;
  emit("done M40 ZERO_ELECTRICAL:%.6f SENSOR_DIRECTION:%s VOLATILE:1 "
       "STATE:%s TRUST:0",
       electricalZero, sensorDirection, semanticState());
}

void Controller::tick(const Observation& observation) {
  const bool activeBefore = exclusiveOperationActive();
  if (exclusiveOperationActive() || state_ == State::Holding ||
      state_ == State::Ready || state_ == State::ArmedUntrusted ||
      state_ == State::ArmedOverridePending) {
    if (!observation.hallValid) {
      hardFault("HALL_INVALID", observation);
      updateCapture(observation, activeBefore);
      updateHeartbeat(observation);
      return;
    }
    if (observation.endstopActive && state_ != State::Homing) {
      hardFault("ENDSTOP_UNEXPECTED", observation);
      updateCapture(observation, activeBefore);
      updateHeartbeat(observation);
      return;
    }
    if (observation.controlDeadlineMissed) {
      hardFault("CONTROL_DEADLINE", observation);
      updateCapture(observation, activeBefore);
      updateHeartbeat(observation);
      return;
    }
    if (provisionalCurrentFault(observation)) {
      hardFault(observation.currentAdcClipped ? "CURRENT_ADC_CLIPPED"
                                              : "CURRENT_FEEDBACK",
                observation);
      updateCapture(observation, activeBefore);
      updateHeartbeat(observation);
      return;
    }
  }

  switch (state_) {
    case State::Homing:
      updateHoming(observation);
      break;
    case State::Moving:
    case State::MovingUntrusted:
    case State::Calibrating:
      updateTravel(observation);
      break;
    case State::TouchingDown:
      updateTouchdown(observation);
      break;
    case State::Holding:
      updateHold(observation);
      break;
    default:
      break;
  }
  updateCapture(observation, activeBefore);
  updateHeartbeat(observation);
}

void Controller::updateCapture(const Observation& observation,
                               const bool activeBefore) {
  if (captureState_ == CaptureState::Armed && activeBefore) {
    captureState_ = CaptureState::Recording;
  }
  if (captureState_ == CaptureState::Recording && activeBefore &&
      captureCount_ < captureLimit_) {
    if (captureTicks_++ % captureDecimation_ == 0U) {
      CaptureSample& sample = capture_[captureCount_++];
      sample = {observation.nowMs,
                static_cast<uint8_t>(state_),
                positionTrusted() ? positionMm(observation) : 0.0F,
                observation.shaftAngleRad,
                observation.velocityRadS,
                drive_.target,
                observation.qCurrentA,
                observation.endstopActive};
    }
  }
  if (captureState_ == CaptureState::Recording &&
      (!exclusiveOperationActive() || captureCount_ >= captureLimit_)) {
    captureState_ = CaptureState::Complete;
  }
}

void Controller::transferCapture() {
  emit("ok M121");
  for (uint16_t index = 0U; index < captureCount_; ++index) {
    const CaptureSample& sample = capture_[index];
    emit("capture I:%u T_MS:%lu STATE_ID:%u Z:%.3f ANGLE:%.6f "
         "VEL_RAD_S:%.3f TARGET:%.6f IQ:%.3f ENDSTOP:%d",
         static_cast<unsigned int>(index),
         static_cast<unsigned long>(sample.timestampMs),
         static_cast<unsigned int>(sample.state), sample.positionMm,
         sample.shaftAngleRad, sample.velocityRadS, sample.target,
         sample.measuredCurrentA, sample.endstop ? 1 : 0);
  }
  emit("done M121 COUNT:%u", static_cast<unsigned int>(captureCount_));
}

void Controller::updateHoming(const Observation& observation) {
  const float phaseDistance =
      fabsf(observation.shaftAngleRad - operationStartMm_) *
      config_.millimetresPerRadian;
  const float maximumSeekDistance =
      fabsf(config_.bottomLimitMm) + fabsf(config_.topLimitMm) + 5.0F;
  if (reachedDeadline(observation) ||
      (homingPhase_ == HomingPhase::Seek &&
       phaseDistance > maximumSeekDistance) ||
      (homingPhase_ == HomingPhase::Release && phaseDistance > 5.0F)) {
    failOperation("HOMING_FAILED", observation);
    return;
  }
  if (homingPhase_ == HomingPhase::Seek && observation.endstopActive) {
    commandPwmOff();
    homingPhase_ = HomingPhase::Release;
    operationStartMm_ = observation.shaftAngleRad;
    drive_ = {DriveMode::Velocity,
              -config_.pullOffSpeedMmS / config_.millimetresPerRadian,
              config_.pullOffSpeedMmS / config_.millimetresPerRadian,
              config_.currentLimitA};
    return;
  }
  if (homingPhase_ == HomingPhase::Release && !observation.endstopActive) {
    homeAngleRad_ = observation.shaftAngleRad;
    homingPhase_ = HomingPhase::PullOff;
    targetMm_ = config_.topLimitMm;
    drive_ = {DriveMode::Position,
              homeAngleRad_ + config_.topLimitMm /
                                  config_.millimetresPerRadian,
              config_.pullOffSpeedMmS / config_.millimetresPerRadian,
              config_.currentLimitA};
    return;
  }
  if (homingPhase_ == HomingPhase::PullOff) {
    const float z = positionMm(observation);
    if (fabsf(z - config_.topLimitMm) <= config_.positionToleranceMm) {
      commandHold(observation);
      state_ = State::Ready;
      char fields[80];
      snprintf(fields, sizeof(fields), "Z:%.3f STATE:READY TRUST:1", z);
      emitDone("G28", fields);
    }
  }
}

void Controller::updateTravel(const Observation& observation) {
  const float position = state_ == State::MovingUntrusted
                             ? (observation.shaftAngleRad -
                                (drive_.target - targetMm_ /
                                                   config_.millimetresPerRadian)) *
                                   config_.millimetresPerRadian
                             : positionMm(observation);
  if (state_ != State::MovingUntrusted &&
      (position < config_.bottomLimitMm - config_.positionToleranceMm ||
       position > config_.topLimitMm + config_.positionToleranceMm)) {
    failOperation("MOVE_FAILED", observation);
    return;
  }
  if (reachedDeadline(observation)) {
    failOperation(state_ == State::Calibrating ? "CALIBRATION_FAILED"
                                                : "MOVE_FAILED",
                  observation);
    return;
  }
  if (fabsf(position - targetMm_) <= config_.positionToleranceMm) {
    if (settleStartedMs_ == 0U) settleStartedMs_ = observation.nowMs;
    if (elapsed(observation.nowMs, settleStartedMs_,
                static_cast<uint32_t>(config_.settleSeconds * 1000.0F))) {
      const bool untrusted = state_ == State::MovingUntrusted;
      const bool calibration = state_ == State::Calibrating;
      const char token[5] = {activeToken_[0], activeToken_[1],
                             activeToken_[2], activeToken_[3], '\0'};
      commandHold(observation);
      state_ = untrusted ? State::ArmedUntrusted : State::Ready;
      char fields[96];
      if (calibration) {
        snprintf(fields, sizeof(fields), "RADIANS:%.6f STATE:READY TRUST:1",
                 calibrationRadians_);
      } else {
        snprintf(fields, sizeof(fields), "Z:%.3f STATE:%s TRUST:%d", position,
                 semanticState(), positionTrusted() ? 1 : 0);
      }
      emitDone(token, fields);
    }
  } else {
    settleStartedMs_ = 0U;
  }

  if (fabsf(position - operationStartMm_) >= config_.positionToleranceMm) {
    operationStartMm_ = position;
    lastProgressMs_ = observation.nowMs;
  } else if (elapsed(observation.nowMs, operationStartedMs_,
                     static_cast<uint32_t>(config_.startupAllowanceSeconds *
                                           1000.0F)) &&
             elapsed(observation.nowMs, lastProgressMs_,
                     static_cast<uint32_t>(config_.progressDwellSeconds *
                                           1000.0F)) &&
             fabsf(observation.qCurrentA) >= config_.currentLimitA * 0.9F) {
    failOperation(state_ == State::Calibrating ? "CALIBRATION_FAILED"
                                                : "MOVE_FAILED",
                  observation);
  }
}

void Controller::updateTouchdown(const Observation& observation) {
  const float position = positionMm(observation);
  if (position <= config_.bottomLimitMm || reachedDeadline(observation)) {
    failOperation("TOUCHDOWN_FAILED", observation);
    return;
  }
  if (touchdownContactDetected_) {
    if (fabsf(position - operationStartMm_) > config_.positionToleranceMm) {
      operationStartMm_ = position;
      settleStartedMs_ = observation.nowMs;
    }
    if (!elapsed(observation.nowMs, settleStartedMs_,
                 static_cast<uint32_t>(config_.settleSeconds * 1000.0F))) {
      return;
    }
    state_ = State::Holding;
    char fields[96];
    snprintf(fields, sizeof(fields),
             "Z:%.3f PRESS:%.1f STATE:HOLDING TRUST:1", position,
             pressPercent_);
    emitDone("G30", fields);
    return;
  }
  if (position < operationStartMm_ - config_.positionToleranceMm) {
    operationStartMm_ = position;
    lastProgressMs_ = observation.nowMs;
  }
  const float selectedCurrent = config_.maximumPressCurrentA *
                                pressPercent_ / 100.0F;
  const bool madeInitialProgress = lastProgressMs_ != operationStartedMs_;
  const bool graceComplete = elapsed(
      observation.nowMs, operationStartedMs_,
      static_cast<uint32_t>(config_.touchdownGraceSeconds * 1000.0F));
  const bool saturated = fabsf(selectedCurrent - fabsf(observation.qCurrentA)) <=
                         config_.touchdownCurrentToleranceA;
  const bool noProgress = elapsed(
      observation.nowMs, lastProgressMs_,
      static_cast<uint32_t>(config_.touchdownDwellSeconds * 1000.0F));
  if (graceComplete && madeInitialProgress && saturated && noProgress) {
    drive_ = {DriveMode::Torque, -selectedCurrent, 0.0F, selectedCurrent};
    operationStartMm_ = position;
    settleStartedMs_ = observation.nowMs;
    touchdownContactDetected_ = true;
  }
}

void Controller::updateHold(const Observation& observation) {
  const float position = positionMm(observation);
  if (position < config_.bottomLimitMm - config_.positionToleranceMm ||
      fabsf(position - operationStartMm_) > config_.positionToleranceMm) {
    failOperation("HOLD_MOVED", observation);
  }
}

bool Controller::provisionalCurrentFault(
    const Observation& observation) const {
  // Ticket 16 replaces this deliberately conservative immediate policy.
  return !observation.currentFinite || observation.currentAdcClipped ||
         !isfinite(observation.qCurrentA) ||
         fabsf(observation.qCurrentA) >= config_.maximumCurrentA;
}

float Controller::provisionalLowSpeedRequest(const float requestedMmS) const {
  // Ticket 17 replaces this direct request; no minimum-speed or open-loop
  // fallback is hidden in the baseline.
  return requestedMmS;
}

void Controller::failOperation(const char* reason,
                               const Observation& observation) {
  const bool trusted = state_ == State::Moving || state_ == State::Calibrating ||
                       state_ == State::TouchingDown || state_ == State::Holding;
  const char token[5] = {activeToken_[0], activeToken_[1], activeToken_[2],
                         activeToken_[3], '\0'};
  commandPwmOff();
  enterIdle(trusted && state_ != State::Homing);
  emitError(token[0] == '\0' ? "OPERATION" : token, reason);
  (void)observation;
}

void Controller::hardFault(const char* reason,
                           const Observation& observation) {
  if (state_ == State::FaultTrusted || state_ == State::FaultUntrusted) return;
  const bool trust = positionTrusted() && strcmp(reason, "HALL_INVALID") != 0 &&
                     strcmp(reason, "ENDSTOP_UNEXPECTED") != 0 &&
                     state_ != State::Homing;
  const char* cancelled = exclusiveOperationActive() ? activeCommand() : "NONE";
  commandPwmOff();
  strncpy(fault_, reason, sizeof(fault_) - 1U);
  enterFault(trust);
  emit("fault %s CANCELLED:%s STATE:FAULT TRUST:%d Z:%s", reason, cancelled,
       trust ? 1 : 0, trust ? "KNOWN" : "?");
  (void)observation;
}

void Controller::commandPwmOff() {
  drive_ = {DriveMode::PwmOff, 0.0F, 0.0F, 0.0F};
}

void Controller::commandHold(const Observation& observation) {
  drive_ = {DriveMode::HoldAngle, observation.shaftAngleRad, 0.0F,
            config_.currentLimitA};
}

void Controller::emitStatus(const Observation& observation,
                            const char* prefix) {
  const bool trust = positionTrusted();
  char z[24];
  if (trust) {
    snprintf(z, sizeof(z), "%.3f", positionMm(observation));
  } else {
    strcpy(z, "?");
  }
  const float velocity = observation.velocityRadS *
                         config_.millimetresPerRadian;
  if (strcmp(prefix, "state") == 0) {
    emit("state STATE:%s TRUST:%d Z:%s VEL:%.3f IQ:%.3f PRESS:%s "
         "ENDSTOP:%d PWM:%s ACTIVE:%s FAULT:%s RUNTIME_MODIFIED:0",
         semanticState(), trust ? 1 : 0, z, velocity,
         isfinite(observation.qCurrentA) ? observation.qCurrentA : 0.0F,
         state_ == State::Holding || state_ == State::TouchingDown
             ? "SET"
             : "?",
         observation.endstopActive ? 1 : 0,
         drive_.mode == DriveMode::PwmOff ? "OFF" : "ACTIVE",
         activeCommand(), fault_[0] == '\0' ? "NONE" : fault_);
  } else {
    emit("status STATE:%s TRUST:%d Z:%s VEL:%.3f IQ:%.3f PRESS:%s "
         "ACTIVE:%s FAULT:%s",
         semanticState(), trust ? 1 : 0, z, velocity,
         isfinite(observation.qCurrentA) ? observation.qCurrentA : 0.0F,
         state_ == State::Holding || state_ == State::TouchingDown
             ? "SET"
             : "?",
         activeCommand(), fault_[0] == '\0' ? "NONE" : fault_);
  }
}

void Controller::updateHeartbeat(const Observation& observation) {
  if (heartbeatIntervalMs_ != 0U &&
      static_cast<int32_t>(observation.nowMs - nextHeartbeatMs_) >= 0) {
    emitStatus(observation, "status");
    nextHeartbeatMs_ = observation.nowMs + heartbeatIntervalMs_;
  }
}

void Controller::emitError(const char* command, const char* reason) {
  emit("error %s REASON:%s", command, reason);
}

void Controller::emitDone(const char* command, const char* fields) {
  if (fields == nullptr || fields[0] == '\0') {
    emit("done %s", command);
  } else if (strncmp(fields, "STATE:", 6U) == 0 || strchr(fields, ':') != nullptr) {
    emit("done %s %s", command, fields);
  } else {
    emit("done %s STATE:%s TRUST:%d", command, fields,
         positionTrusted() ? 1 : 0);
  }
}

void Controller::emit(const char* format, ...) {
  char line[kMaximumResponseLine];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  output_.writeLine(line);
}

void Controller::enterIdle(const bool trusted) {
  commandPwmOff();
  state_ = trusted ? State::IdleTrusted : State::IdleUntrusted;
}

void Controller::enterFault(const bool trusted) {
  commandPwmOff();
  state_ = trusted ? State::FaultTrusted : State::FaultUntrusted;
}

const char* Controller::semanticState() const {
  switch (state_) {
    case State::CommissioningOnly: return "COMMISSIONING_ONLY";
    case State::IdleUntrusted:
    case State::IdleTrusted: return "IDLE";
    case State::ArmedUntrusted: return "ARMED";
    case State::ArmedOverridePending: return "OVERRIDE_PENDING";
    case State::Ready: return "READY";
    case State::AligningCommissioningOnly:
    case State::AligningConfigured: return "ALIGNING";
    case State::Homing: return "HOMING";
    case State::Calibrating: return "CALIBRATING";
    case State::Moving:
    case State::MovingUntrusted: return "MOVING";
    case State::TouchingDown: return "TOUCHING_DOWN";
    case State::Holding: return "HOLDING";
    case State::FaultUntrusted:
    case State::FaultTrusted: return "FAULT";
  }
  return "FAULT";
}

const char* Controller::activeCommand() const {
  return exclusiveOperationActive() ? activeToken_ : "NONE";
}

float Controller::positionMm(const Observation& observation) const {
  return (observation.shaftAngleRad - homeAngleRad_) *
         config_.millimetresPerRadian;
}

uint32_t Controller::secondsToDeadline(const float seconds,
                                       const Observation& observation) const {
  const float bounded = fminf(seconds, config_.maximumOperationSeconds);
  return observation.nowMs + static_cast<uint32_t>(bounded * 1000.0F);
}

bool Controller::reachedDeadline(const Observation& observation) const {
  return static_cast<int32_t>(observation.nowMs - deadlineMs_) >= 0;
}

}  // namespace ligature
