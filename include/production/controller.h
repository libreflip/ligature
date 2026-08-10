#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ligature {

constexpr size_t kMaximumProductionLine = 96U;
constexpr size_t kMaximumResponseLine = 192U;

struct CommissionedConfig {
  bool commissioned;
  float millimetresPerRadian;
  float bottomLimitMm;
  float topLimitMm;
  float positionToleranceMm;
  float fastSpeedMmS;
  float slowSpeedMmS;
  float homingSpeedMmS;
  float pullOffSpeedMmS;
  float currentLimitA;
  float maximumCurrentA;
  float voltageLimitV;
  float maximumVoltageV;
  float maximumSpeedMmS;
  float maximumOperationSeconds;
  float startupAllowanceSeconds;
  float settleSeconds;
  float progressDwellSeconds;
  float touchdownGraceSeconds;
  float touchdownDwellSeconds;
  float touchdownCurrentToleranceA;
  float maximumPressCurrentA;
  float defaultPressPercent;
};

struct Observation {
  uint32_t nowMs;
  float shaftAngleRad;
  float velocityRadS;
  float qCurrentA;
  bool endstopActive;
  bool hallValid;
  bool currentFinite;
  bool currentAdcClipped;
  bool controlDeadlineMissed;
};

enum class DriveMode : uint8_t {
  PwmOff,
  HoldAngle,
  Position,
  Velocity,
  Torque,
  Alignment,
};

struct DriveRequest {
  DriveMode mode;
  float target;
  float speedLimitRadS;
  float currentLimitA;
};

class LineSink {
 public:
  virtual ~LineSink() {}
  virtual void writeLine(const char* line) = 0;
};

enum class State : uint8_t {
  CommissioningOnly,
  IdleUntrusted,
  IdleTrusted,
  ArmedUntrusted,
  ArmedOverridePending,
  Ready,
  AligningCommissioningOnly,
  AligningConfigured,
  Homing,
  Calibrating,
  Moving,
  MovingUntrusted,
  TouchingDown,
  Holding,
  FaultUntrusted,
  FaultTrusted,
};

class Controller {
 public:
  Controller(const CommissionedConfig& config, LineSink& output);

  void reset(const Observation& observation, bool adcOffsetsValid);
  void acceptLine(const char* line, size_t length,
                  const Observation& observation);
  void tick(const Observation& observation);

  // Hardware adapters complete the explicitly supervised operations whose
  // electrical details are intentionally outside the platform-independent core.
  void alignmentComplete(bool passed, float electricalZero,
                         const char* sensorDirection,
                         const Observation& observation);

  State state() const { return state_; }
  DriveRequest driveRequest() const { return drive_; }
  bool positionTrusted() const;
  bool exclusiveOperationActive() const;

 private:
  enum class HomingPhase : uint8_t {
    Seek,
    Release,
    PullOff,
  };

  enum class CaptureState : uint8_t {
    Empty,
    Armed,
    Recording,
    Complete,
  };

  struct CaptureSample {
    uint32_t timestampMs;
    uint8_t state;
    float positionMm;
    float shaftAngleRad;
    float velocityRadS;
    float target;
    float measuredCurrentA;
    bool endstop;
  };

  struct Parameter {
    char letter;
    float value;
  };

  struct ParsedLine {
    char command[5];
    Parameter parameters[3];
    uint8_t parameterCount;
  };

  bool configurationValid() const;
  bool parseLine(const char* line, size_t length, ParsedLine& parsed,
                 const char*& reason) const;
  const Parameter* findParameter(const ParsedLine& parsed, char letter) const;
  bool requireNoParameters(const ParsedLine& parsed);
  bool requireSchema(const ParsedLine& parsed, const char* allowed,
                     char required, const char*& reason) const;
  void dispatch(const ParsedLine& parsed, const Observation& observation);
  void startTravel(const ParsedLine& parsed, const Observation& observation,
                   bool relative);
  void startHoming(const Observation& observation);
  void startTouchdown(const ParsedLine& parsed,
                      const Observation& observation);
  void cancel(const char* command, bool emergency,
              const Observation& observation);
  void failOperation(const char* reason, const Observation& observation);
  void hardFault(const char* reason, const Observation& observation);
  void commandPwmOff();
  void commandHold(const Observation& observation);
  void emitStatus(const Observation& observation, const char* prefix);
  void emitError(const char* command, const char* reason);
  void emitDone(const char* command, const char* fields = nullptr);
  void emit(const char* format, ...);
  void enterIdle(bool trusted);
  void enterFault(bool trusted);
  const char* semanticState() const;
  const char* activeCommand() const;
  float positionMm(const Observation& observation) const;
  uint32_t secondsToDeadline(float seconds,
                             const Observation& observation) const;
  bool reachedDeadline(const Observation& observation) const;
  void updateHeartbeat(const Observation& observation);
  void updateHoming(const Observation& observation);
  void updateTravel(const Observation& observation);
  void updateTouchdown(const Observation& observation);
  void updateHold(const Observation& observation);
  void updateCapture(const Observation& observation, bool activeBefore);
  void transferCapture();

  // These are replacement seams for open policy tickets 16 and 17. No other
  // production logic depends on the provisional policy details.
  bool provisionalCurrentFault(const Observation& observation) const;
  float provisionalLowSpeedRequest(float requestedMmS) const;

  CommissionedConfig config_;
  LineSink& output_;
  State state_;
  HomingPhase homingPhase_;
  DriveRequest drive_;
  float homeAngleRad_;
  float operationStartMm_;
  float targetMm_;
  float pressPercent_;
  float calibrationRadians_;
  uint32_t operationStartedMs_;
  uint32_t deadlineMs_;
  uint32_t lastProgressMs_;
  uint32_t settleStartedMs_;
  uint32_t saturationStartedMs_;
  bool touchdownContactDetected_;
  uint32_t heartbeatIntervalMs_;
  uint32_t nextHeartbeatMs_;
  char activeToken_[5];
  char fault_[32];
  CaptureSample capture_[64];
  CaptureState captureState_;
  uint16_t captureLimit_;
  uint16_t captureCount_;
  uint16_t captureDecimation_;
  uint16_t captureTicks_;
};

}  // namespace ligature
