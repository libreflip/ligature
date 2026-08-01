#include <Arduino.h>
#include <SimpleFOC.h>
#include <esp_system.h>
#include <math.h>
#include <soc/gpio_struct.h>

#include "motor_smoke/alignment_failure.h"
#include "motor_smoke/hall_direction.h"

#ifndef MOTOR_SMOKE_SERIAL_BAUD
#define MOTOR_SMOKE_SERIAL_BAUD 460800UL
#endif

namespace {

// MKS ESP32 FOC V1.0 phase-control nets, from the local board schematic. The
// board has no bridge-enable inputs: software inactivity means all three PWM
// commands are zero. Pulling mains is the only bridge-power isolation.
constexpr uint8_t kMotor0PwmA = 32;
constexpr uint8_t kMotor0PwmB = 33;
constexpr uint8_t kMotor0PwmC = 25;
constexpr uint8_t kMotor1PwmA = 26;
constexpr uint8_t kMotor1PwmB = 27;
constexpr uint8_t kMotor1PwmC = 14;
constexpr uint8_t kMotor1CurrentA = 35;
constexpr uint8_t kMotor1CurrentB = 34;

// Validated Motor 1 ENCODER_1 Hall mapping. External 3.3 V pull-ups are part
// of the tested signal path, so internal pull-ups remain disabled.
constexpr uint8_t kHallPinA = 23;
constexpr uint8_t kHallPinB = 5;
constexpr uint8_t kHallPinC = 13;
constexpr uint8_t kMotorPolePairs = 4;
constexpr uint32_t kHallEdgesPerShaftRevolution = kMotorPolePairs * 6U;

constexpr float kSupplyVoltage = 24.0F;
constexpr float kAlignmentVoltage = 0.9F;
constexpr float kInternalVoltageClamp = 2.0F;
constexpr float kMotionVoltage = 0.9F;
constexpr float kReportedCurrentCutoff = 2.0F;
constexpr float kTargetVelocity = 10.0F;

// The schematic identifies 0.01-ohm shunts. Physical inspection found 1AED
// top marks on both Motor 1 amplifiers, identifying INA181A2 (50 V/V).
// This remains a nominal scale for the smoke test, not calibration.
constexpr float kNominalShuntOhms = 0.01F;
constexpr float kNominalAmplifierGain = 50.0F;

constexpr uint32_t kMotionDurationMs = 2500;
constexpr uint32_t kStopDurationMs = 1200;
constexpr uint32_t kMotionLegalEdgeLimit = kHallEdgesPerShaftRevolution;
constexpr uint32_t kAllowedMinorityHallTransitions = 1;
constexpr uint32_t kOpenLoopVelocityRampMs = 500;
constexpr uint32_t kOpenLoopVoltageRampMs = 50;
constexpr uint32_t kVelocitySettleMs = 300;
constexpr uint32_t kStoppedHallQuietUs = 400000;
constexpr float kMinimumSignedVelocity = 0.05F;
constexpr float kVoltageTolerance = 0.05F;

class ObservedInlineCurrentSense : public InlineCurrentSense {
 public:
  ObservedInlineCurrentSense(const float shuntOhms, const float amplifierGain,
                             const int pinA, const int pinB)
      : InlineCurrentSense(shuntOhms, amplifierGain, pinA, pinB) {}

  PhaseCurrent_s getPhaseCurrents() override {
    const PhaseCurrent_s currents = InlineCurrentSense::getPhaseCurrents();
    observe(currents.a);
    observe(currents.b);
    observe(currents.c);
    ++stageSampleCount;
    ++overallSampleCount;
    return currents;
  }

  void resetAll() {
    overallPeakCurrent = 0.0F;
    overallFinite = true;
    overallSampleCount = 0;
    resetStage();
  }

  void resetStage() {
    stagePeakCurrent = 0.0F;
    stageFinite = true;
    stageSampleCount = 0;
  }

  float stagePeakCurrent = 0.0F;
  float overallPeakCurrent = 0.0F;
  bool stageFinite = true;
  bool overallFinite = true;
  uint32_t stageSampleCount = 0;
  uint32_t overallSampleCount = 0;

 private:
  void observe(const float value) {
    if (!isfinite(value)) {
      stageFinite = false;
      overallFinite = false;
      return;
    }

    const float magnitude = fabsf(value);
    stagePeakCurrent = fmaxf(stagePeakCurrent, magnitude);
    overallPeakCurrent = fmaxf(overallPeakCurrent, magnitude);
  }
};

struct ElectricalStats {
  float stagePeakCurrent = 0.0F;
  float stagePeakQCurrent = 0.0F;
  float stagePeakDCurrent = 0.0F;
  float overallPeakCurrent = 0.0F;
  float stagePeakCommand = 0.0F;
  float stagePeakVoltage = 0.0F;
  float overallPeakVoltage = 0.0F;
  bool stageFinite = true;
  bool overallFinite = true;
  uint32_t stageSamples = 0;
  uint32_t overallSamples = 0;
};

struct VolatileHallStats {
  bool active = false;
  uint8_t previousState = 0;
  uint32_t totalEdges = 0;
  uint32_t legalTransitions = 0;
  uint32_t forwardTransitions = 0;
  uint32_t reverseTransitions = 0;
  uint32_t invalidStates = 0;
  uint32_t invalidTransitions = 0;
  uint32_t lastEdgeUs = 0;
};

struct HallStats {
  int8_t direction = 0;
  uint32_t totalEdges = 0;
  uint32_t legalTransitions = 0;
  uint32_t forwardTransitions = 0;
  uint32_t reverseTransitions = 0;
  uint32_t minorityTransitions = 0;
  uint32_t netTransitions = 0;
  uint32_t invalidStates = 0;
  uint32_t invalidTransitions = 0;
  uint32_t lastEdgeUs = 0;
};

struct MotionResult {
  HallStats hall;
  float averageVelocity = 0.0F;
  float edgeVelocity = 0.0F;
  float peakVelocity = 0.0F;
  float peakCurrent = 0.0F;
  uint32_t velocitySamples = 0;
  const char* failureReason = "NONE";
};

enum class MotionOutcome : uint8_t {
  Passed,
  NoMotion,
  Failed,
};

struct ProbeResult {
  MotionOutcome outcome = MotionOutcome::Failed;
  MotionResult positive;
  MotionResult negative;
  bool stopped = false;
  float motionVoltage = 0.0F;
  const char* failureReason = "NOT_RUN";
};

BLDCDriver3PWM motor1Driver(kMotor1PwmA, kMotor1PwmB, kMotor1PwmC);
BLDCMotor motor1(kMotorPolePairs);
HallSensor hallSensor(kHallPinA, kHallPinB, kHallPinC, kMotorPolePairs);
ObservedInlineCurrentSense currentSense(kNominalShuntOhms,
                                        kNominalAmplifierGain,
                                        kMotor1CurrentA, kMotor1CurrentB);

volatile VolatileHallStats hallCapture;
ElectricalStats electricalStats;

bool driverInitialized = false;
bool currentSenseInitialized = false;
bool runAccepted = false;
char commandBuffer[16] = {};
uint8_t commandLength = 0;

uint8_t IRAM_ATTR readHallStateFast() {
  const uint32_t levels = GPIO.in;
  return static_cast<uint8_t>((((levels >> kHallPinA) & 1U) << 2U) |
                              (((levels >> kHallPinB) & 1U) << 1U) |
                              ((levels >> kHallPinC) & 1U));
}

bool IRAM_ATTR isLegalHallState(const uint8_t state) {
  return state != 0U && state != 7U;
}

int8_t IRAM_ATTR hallStateIndex(const uint8_t state) {
  switch (state) {
    case 1:
      return 0;
    case 5:
      return 1;
    case 4:
      return 2;
    case 6:
      return 3;
    case 2:
      return 4;
    case 3:
      return 5;
    default:
      return -1;
  }
}

int8_t IRAM_ATTR hallTransitionDirection(const uint8_t from,
                                          const uint8_t to) {
  const int8_t fromIndex = hallStateIndex(from);
  const int8_t toIndex = hallStateIndex(to);
  if (fromIndex < 0 || toIndex < 0) {
    return 0;
  }

  const uint8_t delta = static_cast<uint8_t>((toIndex - fromIndex + 6) % 6);
  if (delta == 1U) {
    return 1;
  }
  if (delta == 5U) {
    return -1;
  }
  return 0;
}

void IRAM_ATTR recordHallEdge() {
  if (!hallCapture.active) {
    return;
  }

  const uint8_t state = readHallStateFast();
  const uint8_t previousState = hallCapture.previousState;
  hallCapture.totalEdges = hallCapture.totalEdges + 1U;
  hallCapture.lastEdgeUs = micros();

  if (!isLegalHallState(state)) {
    hallCapture.invalidStates = hallCapture.invalidStates + 1U;
  } else if (!isLegalHallState(previousState)) {
    hallCapture.invalidTransitions = hallCapture.invalidTransitions + 1U;
  } else {
    const uint8_t changedBits = state ^ previousState;
    const bool changedOneBit =
        changedBits != 0U &&
        (changedBits & static_cast<uint8_t>(changedBits - 1U)) == 0U;
    const int8_t direction = hallTransitionDirection(previousState, state);
    if (!changedOneBit || direction == 0) {
      hallCapture.invalidTransitions = hallCapture.invalidTransitions + 1U;
    } else {
      hallCapture.legalTransitions = hallCapture.legalTransitions + 1U;
      if (direction > 0) {
        hallCapture.forwardTransitions =
            hallCapture.forwardTransitions + 1U;
      } else {
        hallCapture.reverseTransitions =
            hallCapture.reverseTransitions + 1U;
      }
    }
  }

  hallCapture.previousState = state;
}

void IRAM_ATTR handleHallA() {
  hallSensor.handleA();
  recordHallEdge();
}

void IRAM_ATTR handleHallB() {
  hallSensor.handleB();
  recordHallEdge();
}

void IRAM_ATTR handleHallC() {
  hallSensor.handleC();
  recordHallEdge();
}

void beginHallCapture() {
  noInterrupts();
  hallCapture.active = false;
  hallCapture.previousState = readHallStateFast();
  hallCapture.totalEdges = 0;
  hallCapture.legalTransitions = 0;
  hallCapture.forwardTransitions = 0;
  hallCapture.reverseTransitions = 0;
  hallCapture.invalidStates = 0;
  hallCapture.invalidTransitions = 0;
  hallCapture.lastEdgeUs = 0;
  hallCapture.active = true;
  interrupts();
}

HallStats snapshotHallCapture(const bool stopCapture) {
  HallStats snapshot;
  noInterrupts();
  if (stopCapture) {
    hallCapture.active = false;
  }
  snapshot.totalEdges = hallCapture.totalEdges;
  snapshot.legalTransitions = hallCapture.legalTransitions;
  snapshot.forwardTransitions = hallCapture.forwardTransitions;
  snapshot.reverseTransitions = hallCapture.reverseTransitions;
  snapshot.invalidStates = hallCapture.invalidStates;
  snapshot.invalidTransitions = hallCapture.invalidTransitions;
  snapshot.lastEdgeUs = hallCapture.lastEdgeUs;
  interrupts();
  const HallDirectionSummary direction = summarizeHallDirection(
      snapshot.forwardTransitions, snapshot.reverseTransitions);
  snapshot.direction = direction.direction;
  snapshot.minorityTransitions = direction.minorityTransitions;
  snapshot.netTransitions = direction.netTransitions;
  return snapshot;
}

const char* hallDirectionName(const int8_t direction) {
  if (direction > 0) {
    return "FORWARD_SEQUENCE";
  }
  if (direction < 0) {
    return "REVERSE_SEQUENCE";
  }
  return "UNKNOWN";
}

const char* sensorDirectionName(const Direction direction) {
  if (direction == Direction::CW) {
    return "CW";
  }
  if (direction == Direction::CCW) {
    return "CCW";
  }
  return "UNKNOWN";
}

void resetElectricalStage() {
  electricalStats.stagePeakCurrent = 0.0F;
  electricalStats.stagePeakQCurrent = 0.0F;
  electricalStats.stagePeakDCurrent = 0.0F;
  electricalStats.stagePeakCommand = 0.0F;
  electricalStats.stagePeakVoltage = 0.0F;
  electricalStats.stageFinite = true;
  electricalStats.stageSamples = 0;
}

void resetAllElectricalStats() {
  electricalStats.overallPeakCurrent = 0.0F;
  electricalStats.overallPeakVoltage = 0.0F;
  electricalStats.overallFinite = true;
  electricalStats.overallSamples = 0;
  resetElectricalStage();
}

void observeElectricalState() {
  const float values[] = {motor1.current.q, motor1.current.d,
                          motor1.voltage.q, motor1.voltage.d,
                          motor1.current_sp, motor1.shaft_velocity};
  for (const float value : values) {
    if (!isfinite(value)) {
      electricalStats.stageFinite = false;
      electricalStats.overallFinite = false;
    }
  }

  if (electricalStats.stageFinite) {
    const float qCurrentMagnitude = fabsf(motor1.current.q);
    const float dCurrentMagnitude = fabsf(motor1.current.d);
    const float currentPeak =
        fmaxf(qCurrentMagnitude, dCurrentMagnitude);
    const float commandMagnitude = fabsf(motor1.current_sp);
    const float voltagePeak =
        fmaxf(fabsf(motor1.voltage.q), fabsf(motor1.voltage.d));
    electricalStats.stagePeakCurrent =
        fmaxf(electricalStats.stagePeakCurrent, currentPeak);
    electricalStats.stagePeakQCurrent =
        fmaxf(electricalStats.stagePeakQCurrent, qCurrentMagnitude);
    electricalStats.stagePeakDCurrent =
        fmaxf(electricalStats.stagePeakDCurrent, dCurrentMagnitude);
    electricalStats.overallPeakCurrent =
        fmaxf(electricalStats.overallPeakCurrent, currentPeak);
    electricalStats.stagePeakCommand =
        fmaxf(electricalStats.stagePeakCommand, commandMagnitude);
    electricalStats.stagePeakVoltage =
        fmaxf(electricalStats.stagePeakVoltage, voltagePeak);
    electricalStats.overallPeakVoltage =
        fmaxf(electricalStats.overallPeakVoltage, voltagePeak);
  }

  ++electricalStats.stageSamples;
  ++electricalStats.overallSamples;
}

float stagePeakCurrent() {
  return fmaxf(currentSense.stagePeakCurrent,
               electricalStats.stagePeakCurrent);
}

float overallPeakCurrent() {
  return fmaxf(currentSense.overallPeakCurrent,
               electricalStats.overallPeakCurrent);
}

const char* activeFaultReason() {
  if (digitalRead(kMotor0PwmA) != LOW || digitalRead(kMotor0PwmB) != LOW ||
      digitalRead(kMotor0PwmC) != LOW) {
    return "MOTOR0_PWM_NOT_LOW";
  }
  if (!motor1.enabled) {
    return "MOTOR1_SOFTWARE_STATE_INACTIVE";
  }
  if (motor1.motor_status != FOCMotorStatus::motor_ready) {
    return "MOTOR_NOT_READY";
  }
  if (!currentSense.stageFinite || !electricalStats.stageFinite) {
    return "NONFINITE_FEEDBACK";
  }
  if (currentSense.stageSampleCount == 0U ||
      electricalStats.stageSamples == 0U) {
    return "NO_CURRENT_FEEDBACK";
  }
  if (electricalStats.stagePeakCurrent >= kReportedCurrentCutoff) {
    return "FILTERED_DQ_CURRENT_AT_OR_ABOVE_LIMIT";
  }
  if (electricalStats.stagePeakVoltage >
      kInternalVoltageClamp + kVoltageTolerance) {
    return "INTERNAL_VOLTAGE_CLAMP_EXCEEDED";
  }

  const HallStats hall = snapshotHallCapture(false);
  if (hall.invalidStates != 0U) {
    return "ILLEGAL_HALL_STATE";
  }
  if (hall.invalidTransitions != 0U) {
    return "ILLEGAL_HALL_TRANSITION";
  }
  return nullptr;
}

void holdMotor0PwmLow() {
  digitalWrite(kMotor0PwmA, LOW);
  digitalWrite(kMotor0PwmB, LOW);
  digitalWrite(kMotor0PwmC, LOW);
}

void commandAllPwmOff() {
  motor1.target = 0.0F;
  motor1.current_sp = 0.0F;
  if (driverInitialized) {
    motor1.disable();
    motor1Driver.disable();
  } else {
    digitalWrite(kMotor1PwmA, LOW);
    digitalWrite(kMotor1PwmB, LOW);
    digitalWrite(kMotor1PwmC, LOW);
  }
  holdMotor0PwmLow();
}

bool motor0PwmLow() {
  return digitalRead(kMotor0PwmA) == LOW &&
         digitalRead(kMotor0PwmB) == LOW &&
         digitalRead(kMotor0PwmC) == LOW;
}

bool motor1PwmZero() {
  if (!driverInitialized) {
    return digitalRead(kMotor1PwmA) == LOW &&
           digitalRead(kMotor1PwmB) == LOW &&
           digitalRead(kMotor1PwmC) == LOW;
  }

  constexpr float kZeroDutyTolerance = 0.0001F;
  return !motor1.enabled && motor1Driver.dc_a <= kZeroDutyTolerance &&
         motor1Driver.dc_b <= kZeroDutyTolerance &&
         motor1Driver.dc_c <= kZeroDutyTolerance;
}

bool allPwmCommandsOff() {
  return motor0PwmLow() && motor1PwmZero();
}

void prepareStageObservations() {
  currentSense.resetStage();
  resetElectricalStage();
  beginHallCapture();
}

MotionOutcome runMotionDirection(const char* directionName,
                                 const float targetVelocity,
                                 MotionResult& result) {
  prepareStageObservations();
  LowPassFilter qCurrentFilter(0.002F);
  LowPassFilter dCurrentFilter(0.002F);
  Serial.printf(
      "STAGE motion direction=%s status=START target_velocity_rad_s=%.2f "
      "motion_voltage_v=%.2f voltage_clamp_v=%.2f current_cutoff_a=%.2f "
      "legal_edge_limit=%lu "
      "velocity_ramp_ms=%lu voltage_ramp_ms=%lu "
      "control=VOLTAGE_OPEN_LOOP\n",
      directionName, targetVelocity, kMotionVoltage, kInternalVoltageClamp,
      kReportedCurrentCutoff,
      static_cast<unsigned long>(kMotionLegalEdgeLimit),
      static_cast<unsigned long>(kOpenLoopVelocityRampMs),
      static_cast<unsigned long>(kOpenLoopVoltageRampMs));

  const uint32_t startedAtMs = millis();
  const uint32_t startedAtUs = micros();
  while (millis() - startedAtMs < kMotionDurationMs) {
    holdMotor0PwmLow();
    const float velocityRampFraction = fminf(
        1.0F, static_cast<float>(millis() - startedAtMs) /
                  static_cast<float>(kOpenLoopVelocityRampMs));
    const float voltageRampFraction = fminf(
        1.0F, static_cast<float>(millis() - startedAtMs) /
                  static_cast<float>(kOpenLoopVoltageRampMs));
    motor1.updateVoltageLimit(kMotionVoltage * voltageRampFraction);
    motor1.loopFOC();
    const float commandedVelocity = targetVelocity * velocityRampFraction;
    motor1.move(commandedVelocity);
    const DQCurrent_s measuredCurrent =
        currentSense.getFOCCurrents(motor1.electrical_angle);
    motor1.current.q = qCurrentFilter(measuredCurrent.q);
    motor1.current.d = dCurrentFilter(measuredCurrent.d);
    observeElectricalState();

    const char* faultReason = activeFaultReason();
    if (faultReason != nullptr) {
      const uint32_t faultElapsedMs = millis() - startedAtMs;
      const float faultQCurrent = motor1.current.q;
      const float faultDCurrent = motor1.current.d;
      const float faultQVoltage = motor1.voltage.q;
      const float faultDVoltage = motor1.voltage.d;
      result.failureReason = faultReason;
      result.hall = snapshotHallCapture(true);
      result.peakCurrent = stagePeakCurrent();
      commandAllPwmOff();
      Serial.printf(
          "STAGE motion direction=%s status=FAIL reason=%s edges=%lu "
          "legal=%lu invalid_states=%lu invalid_transitions=%lu "
          "peak_current_a=%.3f raw_phase_peak_a=%.3f dq_peak_a=%.3f "
          "q_peak_a=%.3f d_peak_a=%.3f q_at_fault_a=%.3f "
          "d_at_fault_a=%.3f q_voltage_at_fault_v=%.3f "
          "d_voltage_at_fault_v=%.3f voltage_command_peak_v=%.3f "
          "dq_voltage_peak_v=%.3f commanded_velocity_rad_s=%.3f "
          "elapsed_ms=%lu current_cutoff_a=%.2f\n",
          directionName, faultReason,
          static_cast<unsigned long>(result.hall.totalEdges),
          static_cast<unsigned long>(result.hall.legalTransitions),
          static_cast<unsigned long>(result.hall.invalidStates),
          static_cast<unsigned long>(result.hall.invalidTransitions),
          result.peakCurrent, currentSense.stagePeakCurrent,
          electricalStats.stagePeakCurrent,
          electricalStats.stagePeakQCurrent,
          electricalStats.stagePeakDCurrent, faultQCurrent, faultDCurrent,
          faultQVoltage, faultDVoltage,
          electricalStats.stagePeakCommand,
          electricalStats.stagePeakVoltage, commandedVelocity,
          static_cast<unsigned long>(faultElapsedMs), kReportedCurrentCutoff);
      return MotionOutcome::Failed;
    }

    if (millis() - startedAtMs >= kVelocitySettleMs &&
        isfinite(motor1.shaft_velocity) &&
        fabsf(motor1.shaft_velocity) >= kMinimumSignedVelocity) {
      result.peakVelocity =
          fmaxf(result.peakVelocity, fabsf(motor1.shaft_velocity));
      ++result.velocitySamples;
    }

    const HallStats liveHall = snapshotHallCapture(false);
    if (liveHall.netTransitions >= kMotionLegalEdgeLimit) {
      break;
    }
  }

  commandAllPwmOff();
  result.hall = snapshotHallCapture(true);
  result.peakCurrent = stagePeakCurrent();
  const uint32_t elapsedUs = static_cast<uint32_t>(micros() - startedAtUs);
  if (elapsedUs != 0U) {
    const float edgeRadians =
        static_cast<float>(result.hall.netTransitions) * _2PI /
        static_cast<float>(kHallEdgesPerShaftRevolution);
    result.edgeVelocity = edgeRadians * 1000000.0F /
                          static_cast<float>(elapsedUs);
    if (result.hall.direction < 0) {
      result.edgeVelocity *= -1.0F;
    }
  }
  if (motor1.controller == MotionControlType::velocity_openloop) {
    result.averageVelocity = result.edgeVelocity;
  }

  if (result.hall.totalEdges == 0U) {
    Serial.printf(
        "STAGE motion direction=%s status=NO_MOTION edges=0 "
        "velocity_rad_s=%.3f edge_velocity_rad_s=%.3f peak_current_a=%.3f "
        "raw_phase_peak_a=%.3f dq_peak_a=%.3f "
        "voltage_command_peak_v=%.3f dq_voltage_peak_v=%.3f\n",
        directionName, result.averageVelocity, result.edgeVelocity,
        result.peakCurrent,
        currentSense.stagePeakCurrent, electricalStats.stagePeakCurrent,
        electricalStats.stagePeakCommand,
        electricalStats.stagePeakVoltage);
    result.failureReason = "NO_HALL_MOTION";
    return MotionOutcome::NoMotion;
  }

  if (result.hall.netTransitions < kMotionLegalEdgeLimit ||
      result.hall.direction == 0 ||
      result.hall.minorityTransitions > kAllowedMinorityHallTransitions ||
      result.velocitySamples == 0U) {
    result.failureReason = result.hall.netTransitions < kMotionLegalEdgeLimit
                               ? "MOTION_EDGE_LIMIT_NOT_REACHED"
                               : "IMPLAUSIBLE_HALL_SEQUENCE";
    Serial.printf(
        "STAGE motion direction=%s status=FAIL reason=%s edges=%lu "
        "legal=%lu forward_edges=%lu reverse_edges=%lu minority_edges=%lu "
        "net_edges=%lu hall_direction=%s "
        "velocity_rad_s=%.3f edge_velocity_rad_s=%.3f "
        "velocity_samples=%lu peak_current_a=%.3f "
        "raw_phase_peak_a=%.3f dq_peak_a=%.3f "
        "voltage_command_peak_v=%.3f dq_voltage_peak_v=%.3f\n",
        directionName, result.failureReason,
        static_cast<unsigned long>(result.hall.totalEdges),
        static_cast<unsigned long>(result.hall.legalTransitions),
        static_cast<unsigned long>(result.hall.forwardTransitions),
        static_cast<unsigned long>(result.hall.reverseTransitions),
        static_cast<unsigned long>(result.hall.minorityTransitions),
        static_cast<unsigned long>(result.hall.netTransitions),
        hallDirectionName(result.hall.direction), result.averageVelocity,
        result.edgeVelocity,
        static_cast<unsigned long>(result.velocitySamples), result.peakCurrent,
        currentSense.stagePeakCurrent, electricalStats.stagePeakCurrent,
        electricalStats.stagePeakCommand,
        electricalStats.stagePeakVoltage);
    return MotionOutcome::Failed;
  }

  Serial.printf(
      "STAGE motion direction=%s status=PASS edges=%lu legal=%lu "
      "forward_edges=%lu reverse_edges=%lu minority_edges=%lu net_edges=%lu "
      "hall_direction=%s velocity_rad_s=%.3f edge_velocity_rad_s=%.3f "
      "peak_velocity_rad_s=%.3f "
      "peak_current_a=%.3f raw_phase_peak_a=%.3f dq_peak_a=%.3f "
      "voltage_command_peak_v=%.3f dq_voltage_peak_v=%.3f "
      "current_finite=YES current_below_limit=YES current_cutoff_a=%.2f\n",
      directionName, static_cast<unsigned long>(result.hall.totalEdges),
      static_cast<unsigned long>(result.hall.legalTransitions),
      static_cast<unsigned long>(result.hall.forwardTransitions),
      static_cast<unsigned long>(result.hall.reverseTransitions),
      static_cast<unsigned long>(result.hall.minorityTransitions),
      static_cast<unsigned long>(result.hall.netTransitions),
      hallDirectionName(result.hall.direction), result.averageVelocity,
      result.edgeVelocity, result.peakVelocity, result.peakCurrent,
      currentSense.stagePeakCurrent, electricalStats.stagePeakCurrent,
      electricalStats.stagePeakCommand,
      electricalStats.stagePeakVoltage, kReportedCurrentCutoff);
  result.failureReason = "NONE";
  return MotionOutcome::Passed;
}

bool observePwmOffCoast(const char* afterDirection,
                        const char*& failureReason) {
  commandAllPwmOff();
  prepareStageObservations();
  Serial.printf(
      "STAGE coast after=%s status=START pwm_state=OFF duration_ms=%lu\n",
      afterDirection, static_cast<unsigned long>(kStopDurationMs));

  const uint32_t startedAtMs = millis();
  while (millis() - startedAtMs < kStopDurationMs) {
    holdMotor0PwmLow();
    const HallStats liveHall = snapshotHallCapture(false);
    if (liveHall.invalidStates != 0U ||
        liveHall.invalidTransitions != 0U) {
      if (liveHall.invalidStates != 0U) {
        failureReason = "COAST_ILLEGAL_HALL_STATE";
      } else {
        failureReason = "COAST_ILLEGAL_HALL_TRANSITION";
      }
      const HallStats hall = snapshotHallCapture(true);
      Serial.printf(
          "STAGE coast after=%s status=FAIL reason=%s edges=%lu legal=%lu "
          "forward_edges=%lu reverse_edges=%lu hall_direction=%s "
          "minority_edges=%lu pwm_state=OFF\n",
          afterDirection, failureReason,
          static_cast<unsigned long>(hall.totalEdges),
          static_cast<unsigned long>(hall.legalTransitions),
          static_cast<unsigned long>(hall.forwardTransitions),
          static_cast<unsigned long>(hall.reverseTransitions),
          hallDirectionName(hall.direction),
          static_cast<unsigned long>(hall.minorityTransitions));
      return false;
    }
    delay(1);
  }

  const HallStats hall = snapshotHallCapture(true);
  const uint32_t quietTimeUs =
      hall.lastEdgeUs == 0U ? kStoppedHallQuietUs
                            : static_cast<uint32_t>(micros() - hall.lastEdgeUs);
  if (quietTimeUs < kStoppedHallQuietUs) {
    failureReason = "COAST_NOT_QUIET";
    Serial.printf(
        "STAGE coast after=%s status=FAIL reason=%s edges=%lu legal=%lu "
        "forward_edges=%lu reverse_edges=%lu hall_direction=%s "
        "minority_edges=%lu quiet_us=%lu pwm_state=OFF\n",
        afterDirection, failureReason,
        static_cast<unsigned long>(hall.totalEdges),
        static_cast<unsigned long>(hall.legalTransitions),
        static_cast<unsigned long>(hall.forwardTransitions),
        static_cast<unsigned long>(hall.reverseTransitions),
        hallDirectionName(hall.direction),
        static_cast<unsigned long>(hall.minorityTransitions),
        static_cast<unsigned long>(quietTimeUs));
    return false;
  }

  failureReason = "NONE";
  Serial.printf(
      "STAGE coast after=%s status=PASS edges=%lu legal=%lu "
      "forward_edges=%lu reverse_edges=%lu hall_direction=%s "
      "minority_edges=%lu quiet_us=%lu pwm_state=OFF\n",
      afterDirection, static_cast<unsigned long>(hall.totalEdges),
      static_cast<unsigned long>(hall.legalTransitions),
      static_cast<unsigned long>(hall.forwardTransitions),
      static_cast<unsigned long>(hall.reverseTransitions),
      hallDirectionName(hall.direction),
      static_cast<unsigned long>(hall.minorityTransitions),
      static_cast<unsigned long>(quietTimeUs));
  return true;
}

ProbeResult runVoltageOpenLoopSmoke() {
  ProbeResult probe;
  probe.motionVoltage = kMotionVoltage;
  motor1.updateTorqueControlType(TorqueControlType::voltage);
  motor1.updateVoltageLimit(kMotionVoltage);
  motor1.updateMotionControlType(MotionControlType::velocity_openloop);

  const MotionOutcome positiveOutcome = runMotionDirection(
      "POSITIVE_VOLTAGE_OPEN_LOOP", kTargetVelocity, probe.positive);
  if (positiveOutcome != MotionOutcome::Passed) {
    probe.outcome = positiveOutcome;
    probe.failureReason = probe.positive.failureReason;
    return probe;
  }

  if (!observePwmOffCoast("POSITIVE_VOLTAGE_OPEN_LOOP",
                          probe.failureReason)) {
    probe.outcome = MotionOutcome::Failed;
    return probe;
  }

  motor1.enable();
  const MotionOutcome negativeOutcome = runMotionDirection(
      "NEGATIVE_VOLTAGE_OPEN_LOOP", -kTargetVelocity, probe.negative);
  if (negativeOutcome != MotionOutcome::Passed) {
    probe.outcome = negativeOutcome;
    probe.failureReason = probe.negative.failureReason;
    return probe;
  }

  if (!observePwmOffCoast("NEGATIVE_VOLTAGE_OPEN_LOOP",
                          probe.failureReason)) {
    probe.outcome = MotionOutcome::Failed;
    return probe;
  }

  probe.stopped = true;
  if (probe.positive.hall.direction == probe.negative.hall.direction) {
    probe.failureReason = "HALL_DIRECTIONS_NOT_OPPOSITE";
    return probe;
  }

  probe.outcome = MotionOutcome::Passed;
  probe.failureReason = "NONE";
  return probe;
}

void printTerminalResult(const bool passed, const char* stage,
                         const char* reason, const ProbeResult* probe) {
  commandAllPwmOff();
  const bool pwmOff = allPwmCommandsOff();

  const float motionVoltage =
      probe == nullptr ? 0.0F : probe->motionVoltage;
  const uint32_t positiveEdges =
      probe == nullptr ? 0U : probe->positive.hall.legalTransitions;
  const uint32_t negativeEdges =
      probe == nullptr ? 0U : probe->negative.hall.legalTransitions;
  const float positiveVelocity =
      probe == nullptr ? 0.0F : probe->positive.averageVelocity;
  const float negativeVelocity =
      probe == nullptr ? 0.0F : probe->negative.averageVelocity;
  const bool stopped = probe != nullptr && probe->stopped;

  Serial.printf(
      "RESULT status=%s stage=%s fault=%s reset=NONE_DURING_RUN "
      "motion_voltage_v=%.2f current_cutoff_a=%.2f "
      "positive_edges=%lu negative_edges=%lu "
      "positive_velocity_rad_s=%.3f negative_velocity_rad_s=%.3f "
      "peak_current_a=%.3f stopped=%s motor0_pwm=%s motor1_pwm=%s "
      "pwm_state=%s isolation=MAINS_ONLY\n",
      passed && pwmOff ? "PASS" : "FAIL", stage,
      pwmOff ? reason : "FINAL_PWM_OFF_FAILED", motionVoltage,
      kReportedCurrentCutoff,
      static_cast<unsigned long>(positiveEdges),
      static_cast<unsigned long>(negativeEdges), positiveVelocity,
      negativeVelocity, overallPeakCurrent(), stopped ? "YES" : "NO",
      motor0PwmLow() ? "LOW" : "NOT_LOW",
      motor1PwmZero() ? "ZERO" : "NONZERO",
      pwmOff ? "OFF" : "NOT_OFF");
}

void runSmokeTest() {
  Serial.println("RUN accepted");
  currentSense.resetAll();
  resetAllElectricalStats();

  if (!driverInitialized) {
    printTerminalResult(false, "startup", "DRIVER_INIT_FAILED", nullptr);
    return;
  }
  if (!currentSenseInitialized) {
    printTerminalResult(false, "startup", "CURRENT_SENSE_INIT_FAILED",
                        nullptr);
    return;
  }

  const uint8_t alignmentStartHallState = readHallStateFast();
  beginHallCapture();
  Serial.printf(
      "STAGE alignment status=START alignment_voltage_v=%.2f "
      "voltage_clamp_v=%.2f pwm_state=ALIGNING isolation=MAINS_ONLY\n",
      kAlignmentVoltage, kInternalVoltageClamp);

  if (!motor1.init()) {
    snapshotHallCapture(true);
    printTerminalResult(false, "alignment", "MOTOR_INIT_FAILED", nullptr);
    return;
  }
  const int alignmentResult = motor1.initFOC();
  const HallStats alignmentHall = snapshotHallCapture(true);
  const uint8_t alignmentEndHallState = readHallStateFast();
  const float alignmentPeakCurrent = currentSense.stagePeakCurrent;
  const bool alignmentHallLegal = alignmentHall.invalidStates == 0U &&
                                  alignmentHall.invalidTransitions == 0U;
  const AlignmentEvidence alignmentEvidence = {
      .focResult = alignmentResult,
      .hallEdges = alignmentHall.totalEdges,
      .hallStartLegal = isLegalHallState(alignmentStartHallState),
      .hallLegal = alignmentHallLegal,
      .currentSamples = currentSense.stageSampleCount,
      .currentFinite = currentSense.stageFinite,
      .peakCurrent = alignmentPeakCurrent,
      .currentCutoff = kReportedCurrentCutoff,
  };
  const char* alignmentFailure =
      classifyAlignmentFailure(alignmentEvidence);

  if (alignmentFailure != nullptr) {
    const char* currentStage = currentSense.stageSampleCount == 0U
                                   ? "NOT_REACHED"
                                   : (alignmentResult != 0 ? "PASS" : "FAILED");
    Serial.printf(
        "STAGE alignment status=FAIL reason=%s result=%d edges=%lu "
        "invalid_states=%lu invalid_transitions=%lu "
        "hall_start_abc=%u%u%u hall_end_abc=%u%u%u "
        "current_stage=%s current_samples=%lu peak_current_a=%.3f "
        "motor_status=%u\n",
        alignmentFailure, alignmentResult,
        static_cast<unsigned long>(alignmentHall.totalEdges),
        static_cast<unsigned long>(alignmentHall.invalidStates),
        static_cast<unsigned long>(alignmentHall.invalidTransitions),
        (alignmentStartHallState >> 2U) & 1U,
        (alignmentStartHallState >> 1U) & 1U,
        alignmentStartHallState & 1U,
        (alignmentEndHallState >> 2U) & 1U,
        (alignmentEndHallState >> 1U) & 1U,
        alignmentEndHallState & 1U, currentStage,
        static_cast<unsigned long>(currentSense.stageSampleCount),
        alignmentPeakCurrent, static_cast<unsigned int>(motor1.motor_status));
    printTerminalResult(false, "alignment", alignmentFailure, nullptr);
    return;
  }

  Serial.printf(
      "STAGE alignment status=PASS result=%d sensor_direction=%s "
      "zero_electric_angle_rad=%.6f hall_edges=%lu invalid_states=0 "
      "invalid_transitions=0 current_pin_a=%d current_pin_b=%d "
      "current_gain_a=%.3f current_gain_b=%.3f peak_current_a=%.3f "
      "current_scale=NOMINAL_UNCALIBRATED\n",
      alignmentResult, sensorDirectionName(motor1.sensor_direction),
      motor1.zero_electric_angle,
      static_cast<unsigned long>(alignmentHall.totalEdges), currentSense.pinA,
      currentSense.pinB, currentSense.gain_a, currentSense.gain_b,
      alignmentPeakCurrent);

  ProbeResult probe = runVoltageOpenLoopSmoke();
  if (probe.outcome != MotionOutcome::Passed || !probe.stopped) {
    printTerminalResult(false, "voltage_open_loop", probe.failureReason,
                        &probe);
    return;
  }

  printTerminalResult(true, "voltage_open_loop", "NONE", &probe);
}

void processCommandLine() {
  commandBuffer[commandLength] = '\0';
  if (!runAccepted && strcmp(commandBuffer, "RUN") == 0) {
    runAccepted = true;
    commandLength = 0;
    runSmokeTest();
    return;
  }

  Serial.printf("REJECT command=%s reason=%s\n", commandBuffer,
                runAccepted ? "ALREADY_RUN" : "ONLY_RUN_ALLOWED");
  commandLength = 0;
}

void processSerialInput() {
  while (Serial.available() > 0) {
    const char input = static_cast<char>(Serial.read());
    if (input == '\r') {
      continue;
    }
    if (input == '\n') {
      if (commandLength != 0U) {
        processCommandLine();
      }
      continue;
    }
    if (commandLength < sizeof(commandBuffer) - 1U) {
      commandBuffer[commandLength++] = input;
    } else {
      commandLength = 0;
      Serial.println("REJECT command=TOO_LONG reason=ONLY_RUN_ALLOWED");
    }
  }
}

}  // namespace

void setup() {
  // Establish zero-output latches before making any phase-control pin an
  // output. GPIO 21/22 are no-connects on this board, not bridge enables.
  digitalWrite(kMotor0PwmA, LOW);
  digitalWrite(kMotor0PwmB, LOW);
  digitalWrite(kMotor0PwmC, LOW);
  digitalWrite(kMotor1PwmA, LOW);
  digitalWrite(kMotor1PwmB, LOW);
  digitalWrite(kMotor1PwmC, LOW);
  pinMode(kMotor0PwmA, OUTPUT);
  pinMode(kMotor0PwmB, OUTPUT);
  pinMode(kMotor0PwmC, OUTPUT);
  pinMode(kMotor1PwmA, OUTPUT);
  pinMode(kMotor1PwmB, OUTPUT);
  pinMode(kMotor1PwmC, OUTPUT);

  hallSensor.pullup = Pullup::USE_EXTERN;
  hallSensor.init();
  hallSensor.enableInterrupts(handleHallA, handleHallB, handleHallC);

  Serial.begin(MOTOR_SMOKE_SERIAL_BAUD);
  delay(100);
  Serial.printf("BOOT app=motor-smoke reset_reason=%d\n",
                static_cast<int>(esp_reset_reason()));

  motor1Driver.voltage_power_supply = kSupplyVoltage;
  motor1Driver.voltage_limit = kInternalVoltageClamp;
  driverInitialized = motor1Driver.init();
  motor1Driver.disable();

  motor1.linkSensor(&hallSensor);
  motor1.linkDriver(&motor1Driver);
  currentSense.linkDriver(&motor1Driver);
  if (driverInitialized) {
    currentSenseInitialized = currentSense.init();
  }
  motor1Driver.disable();
  motor1.linkCurrentSense(&currentSense);

  motor1.torque_controller = TorqueControlType::voltage;
  motor1.controller = MotionControlType::velocity_openloop;
  motor1.voltage_sensor_align = kAlignmentVoltage;
  motor1.voltage_limit = kMotionVoltage;
  motor1.velocity_limit = kTargetVelocity;
  motor1.feed_forward_current = {0.0F, 0.0F};
  motor1.feed_forward_voltage = {0.0F, 0.0F};

  // Current feedback is monitoring-only in this voltage-mode smoke test.
  motor1.LPF_current_q.Tf = 0.002F;
  motor1.LPF_current_d.Tf = 0.002F;

  commandAllPwmOff();
  Serial.printf(
      "READY app=motor-smoke baud=%lu simplefoc=2.4.0 motor0_pwm=LOW "
      "motor1_pwm=ZERO pwm_state=OFF isolation=MAINS_ONLY driver_init=%s "
      "current_sense_init=%s command=RUN\n",
      static_cast<unsigned long>(MOTOR_SMOKE_SERIAL_BAUD),
      driverInitialized ? "PASS" : "FAIL",
      currentSenseInitialized ? "PASS" : "FAIL");
}

void loop() {
  holdMotor0PwmLow();
  processSerialInput();
}
