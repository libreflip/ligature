#include <Arduino.h>
#include <SimpleFOC.h>
#include <esp_system.h>
#include <math.h>

#include "motor_tuning/command_policy.h"
#include "serial_baud.h"

namespace {

constexpr uint8_t kMotor0PwmA = 32;
constexpr uint8_t kMotor0PwmB = 33;
constexpr uint8_t kMotor0PwmC = 25;
constexpr uint8_t kMotor1PwmA = 26;
constexpr uint8_t kMotor1PwmB = 27;
constexpr uint8_t kMotor1PwmC = 14;
constexpr uint8_t kMotor1CurrentA = 35;
constexpr uint8_t kMotor1CurrentB = 34;
constexpr uint8_t kHallPinA = 23;
constexpr uint8_t kHallPinB = 5;
constexpr uint8_t kHallPinC = 13;
constexpr uint8_t kMotorPolePairs = 4;

constexpr float kMotorHallSectorAngle =
    6.28318530718F / (6.0F * kMotorPolePairs);
constexpr float kSupplyVoltage = 24.0F;
constexpr float kAlignmentVoltage = 1.5F;
constexpr float kDefaultVoltageLimit = 3.0F;
constexpr float kDefaultCurrentLimit = 1.5F;
constexpr float kDefaultVelocityLimit = 100.0F;
constexpr float kDefaultCurrentOutputRamp = 10000.0F;
constexpr float kNominalShuntOhms = 0.01F;
constexpr float kNominalAmplifierGain = 50.0F;
constexpr uint8_t kSerialBytesPerLoop = 8U;

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
    ++sampleCount;
    return currents;
  }

  void resetObservations() {
    peakCurrent = 0.0F;
    finite = true;
    sampleCount = 0U;
  }

  float peakCurrent = 0.0F;
  bool finite = true;
  uint32_t sampleCount = 0U;

 private:
  void observe(const float value) {
    if (!isfinite(value)) {
      finite = false;
    } else {
      peakCurrent = fmaxf(peakCurrent, fabsf(value));
    }
  }
};

BLDCDriver3PWM motor1Driver(kMotor1PwmA, kMotor1PwmB, kMotor1PwmC);
BLDCMotor motor1(kMotorPolePairs);
HallSensor hallSensor(kHallPinA, kHallPinB, kHallPinC, kMotorPolePairs);
ObservedInlineCurrentSense currentSense(kNominalShuntOhms,
                                        kNominalAmplifierGain,
                                        kMotor1CurrentA, kMotor1CurrentB);
Commander commander(Serial);

TuningSessionState sessionState = TuningSessionState::Motionless;
TuningMotionMode motionMode = TuningMotionMode::Velocity;
bool driverInitialized = false;
bool currentSenseInitialized = false;
bool motorInitialized = false;
bool alignmentValid = false;
uint32_t previousControlUs = 0U;
uint32_t maximumControlPeriodUs = 0U;
volatile uint32_t invalidHallTransitions = 0U;
char serialLine[32] = {};
uint8_t serialLineLength = 0U;

void holdMotor0PwmLow() {
  digitalWrite(kMotor0PwmA, LOW);
  digitalWrite(kMotor0PwmB, LOW);
  digitalWrite(kMotor0PwmC, LOW);
}

void commandAllPwmOff() {
  const bool angleMode = motionMode == TuningMotionMode::Angle ||
                         motionMode == TuningMotionMode::AngleOpenLoop ||
                         motionMode == TuningMotionMode::AngleNoCascade;
  motor1.target = alignmentValid && angleMode ? motor1.shaft_angle : 0.0F;
  motor1.current_sp = 0.0F;
  if (motorInitialized) {
    motor1.disable();
  }
  if (driverInitialized) {
    motor1Driver.disable();
  } else {
    digitalWrite(kMotor1PwmA, LOW);
    digitalWrite(kMotor1PwmB, LOW);
    digitalWrite(kMotor1PwmC, LOW);
  }
  holdMotor0PwmLow();
}

const char* sessionName() {
  switch (sessionState) {
    case TuningSessionState::Motionless:
      return "MOTIONLESS";
    case TuningSessionState::Aligned:
      return "ALIGNED_DISARMED";
    case TuningSessionState::Armed:
      return "ARMED";
    case TuningSessionState::Fault:
      return "FAULT";
  }
  return "UNKNOWN";
}

bool motor1PwmZero() {
  constexpr float kTolerance = 0.0001F;
  return !motor1.enabled && motor1Driver.dc_a <= kTolerance &&
         motor1Driver.dc_b <= kTolerance && motor1Driver.dc_c <= kTolerance;
}

void disarm(const TuningSessionState nextState, const char* reason) {
  commandAllPwmOff();
  sessionState = nextState;
  Serial.printf("!DISARM reason=%s state=%s pwm_state=OFF\n", reason,
                sessionName());
}

void IRAM_ATTR recordHallTransition(const int8_t previousSector) {
  const int8_t currentSector = hallSensor.electric_sector;
  if (previousSector < 0 || currentSector < 0 ||
      previousSector == currentSector) {
    return;
  }
  const uint8_t delta =
      static_cast<uint8_t>((currentSector - previousSector + 6) % 6);
  if (delta != 1U && delta != 5U) {
    invalidHallTransitions = invalidHallTransitions + 1U;
  }
}

void IRAM_ATTR hallA() {
  const int8_t previousSector = hallSensor.electric_sector;
  hallSensor.handleA();
  recordHallTransition(previousSector);
}
void IRAM_ATTR hallB() {
  const int8_t previousSector = hallSensor.electric_sector;
  hallSensor.handleB();
  recordHallTransition(previousSector);
}
void IRAM_ATTR hallC() {
  const int8_t previousSector = hallSensor.electric_sector;
  hallSensor.handleC();
  recordHallTransition(previousSector);
}

void printStatus() {
  Serial.printf(
      "!STATUS state=%s pwm_state=%s target=%.5f current_limit_a=%.5f "
      "voltage_limit_v=%.5f velocity_limit_rad_s=%.5f "
      "loop_period_us=%lu max_loop_period_us=%lu monitor_downsample=%u\n",
      sessionName(), motor1PwmZero() ? "OFF" : "ACTIVE", motor1.target,
      motor1.current_limit, motor1.voltage_limit, motor1.velocity_limit,
      static_cast<unsigned long>(motor1.loopfoc_time_us),
      static_cast<unsigned long>(maximumControlPeriodUs),
      motor1.monitor_downsample);
}

void runBoundedAlignment() {
  if (sessionState != TuningSessionState::Motionless &&
      sessionState != TuningSessionState::Fault) {
    Serial.printf("!REJECT command=XALIGN reason=DISARM_REQUIRED state=%s\n",
                  sessionName());
    return;
  }
  if (!driverInitialized || !currentSenseInitialized) {
    disarm(TuningSessionState::Fault, "HARDWARE_INIT_FAILED");
    return;
  }

  Serial.printf(
      "!ALIGN status=START voltage_v=%.2f current_cutoff_a=%.2f "
      "isolation=MAINS_ONLY\n",
      kAlignmentVoltage, kTuningCurrentCeiling);
  motor1.monitor_variables = 0U;
  motor1.monitor_downsample = 0U;
  alignmentValid = false;
  invalidHallTransitions = 0U;
  currentSense.resetObservations();
  motor1.voltage_sensor_align = kAlignmentVoltage;
  motor1.updateVoltageLimit(kDefaultVoltageLimit);
  if (!motorInitialized) {
    motorInitialized = motor1.init() != 0;
  }
  if (motorInitialized) {
    motor1.sensor_direction = Direction::UNKNOWN;
    motor1.zero_electric_angle = NOT_SET;
  }
  const int focResult = motorInitialized ? motor1.initFOC() : 0;
  commandAllPwmOff();

  const bool passed = focResult != 0 && motor1.pp_check_result &&
                      currentSense.finite && currentSense.sampleCount != 0U &&
                      currentSense.peakCurrent < kTuningCurrentCeiling &&
                      hallSensor.electric_sector >= 0 &&
                      invalidHallTransitions == 0U;
  previousControlUs = 0U;
  maximumControlPeriodUs = 0U;
  if (!passed) {
    sessionState = TuningSessionState::Fault;
    Serial.printf(
        "!ALIGN status=FAIL result=%d pp_check=%s current_finite=%s "
        "samples=%lu peak_phase_current_a=%.3f hall_sector=%d "
        "invalid_hall_transitions=%lu pwm_state=OFF state=FAULT\n",
        focResult, motor1.pp_check_result ? "PASS" : "FAIL",
        currentSense.finite ? "YES" : "NO",
        static_cast<unsigned long>(currentSense.sampleCount),
        currentSense.peakCurrent, hallSensor.electric_sector,
        static_cast<unsigned long>(invalidHallTransitions));
    return;
  }

  alignmentValid = true;
  sessionState = TuningSessionState::Aligned;
  motor1.updateTorqueControlType(TorqueControlType::foc_current);
  motor1.updateMotionControlType(MotionControlType::velocity);
  motionMode = TuningMotionMode::Velocity;
  Serial.printf(
      "!ALIGN status=PASS result=%d peak_phase_current_a=%.3f "
      "zero_electric_angle_rad=%.6f pwm_state=OFF "
      "state=ALIGNED_DISARMED\n",
      focResult, currentSense.peakCurrent, motor1.zero_electric_angle);
}

void enterArmedState() {
  currentSense.resetObservations();
  motor1.PID_velocity.reset();
  motor1.PID_current_q.reset();
  motor1.PID_current_d.reset();
  motor1.enable();
  sessionState = TuningSessionState::Armed;
  previousControlUs = 0U;
  maximumControlPeriodUs = 0U;
}

void processControlCommand(const char* line) {
  if (strcmp(line, "XALIGN") == 0) {
    runBoundedAlignment();
  } else if (strcmp(line, "XSTATUS") == 0) {
    printStatus();
  } else {
    Serial.printf("!REJECT command=%s reason=UNKNOWN_CONTROL_COMMAND\n", line);
  }
}

void makeReadCommand(const char* command, char* readCommand) {
  const size_t length = tuningCommandLength(command);
  size_t prefixLength = 0U;
  if (length >= 2U &&
      (strchr("AQDV", command[0]) != nullptr || command[0] == 'L' ||
       command[0] == 'M')) {
    prefixLength = 2U;
    if (command[0] == 'M' && command[1] == 'G' && length >= 3U) {
      prefixLength = 3U;
    }
  } else if (length >= 1U && (command[0] == 'C' || command[0] == 'T' ||
                              command[0] == 'E')) {
    prefixLength = 1U;
  }
  memcpy(readCommand, command, prefixLength);
  readCommand[prefixLength] = '\n';
  readCommand[prefixLength + 1U] = '\0';
}

void motorAdapter(char* command) {
  const TuningPolicyContext context = {sessionState, motionMode};
  const TuningCommandDecision decision =
      evaluateTuningMotorCommand(command, context);
  if (!decision.accepted) {
    char readCommand[5] = {};
    makeReadCommand(command, readCommand);
    if (readCommand[0] != '\n' ||
        decision.kind == TuningCommandKind::Target) {
      commander.motor(&motor1, readCommand);
    } else {
      Serial.println("err");
    }
    char printable[24] = {};
    const size_t commandLength = tuningCommandLength(command);
    const size_t length = commandLength < sizeof(printable) - 1U
                              ? commandLength
                              : sizeof(printable) - 1U;
    memcpy(printable, command, length);
    Serial.printf("!REJECT command=M%s reason=%s\n", printable,
                  decision.reason);
    return;
  }

  if (decision.kind == TuningCommandKind::MotorEnable) {
    if (decision.value == 1.0F) {
      if (sessionState == TuningSessionState::Aligned) {
        enterArmedState();
      }
    } else {
      commandAllPwmOff();
      if (sessionState == TuningSessionState::Armed) {
        sessionState = TuningSessionState::Aligned;
      } else if (sessionState == TuningSessionState::Fault) {
        const bool faultClearable = canClearTuningFault(
            alignmentValid, hallSensor.electric_sector >= 0,
            motor1.motor_status == FOCMotorStatus::motor_ready);
        if (faultClearable) {
          invalidHallTransitions = 0U;
          currentSense.resetObservations();
          motor1.PID_velocity.reset();
          motor1.PID_current_q.reset();
          motor1.PID_current_d.reset();
          sessionState = TuningSessionState::Aligned;
        }
      }
    }
    char statusRead[] = "E\n";
    commander.motor(&motor1, statusRead);
    Serial.printf("!INTERFACE_ARM state=%s pwm_state=%s\n", sessionName(),
                  motor1PwmZero() ? "OFF" : "ACTIVE");
    return;
  }

  commander.motor(&motor1, command);
  motionMode = static_cast<TuningMotionMode>(motor1.controller);
}

void processLine() {
  serialLine[serialLineLength] = '\0';
  if (serialLine[0] == 'X') {
    processControlCommand(serialLine);
  } else {
    // Stock WebController sends decimal and verbosity setup immediately after
    // each open. Treat that connection handshake as a reconnect boundary even
    // on USB/UART bridges that do not expose DTR state to Arduino.
    if (sessionState == TuningSessionState::Armed &&
        (serialLine[0] == '#' || serialLine[0] == '@')) {
      disarm(TuningSessionState::Aligned, "CONNECTION_REINITIALIZED");
    }
    const TuningMonitorFraming framing =
        tuningMonitorFramingCommand(serialLine);
    if (framing == TuningMonitorFraming::WebController) {
      motor1.monitor_start_char = 'M';
      motor1.monitor_end_char = 'M';
    } else if (framing == TuningMonitorFraming::Studio) {
      motor1.monitor_start_char = '\0';
      motor1.monitor_end_char = '\0';
    }
    serialLine[serialLineLength] = '\n';
    serialLine[serialLineLength + 1U] = '\0';
    commander.run(serialLine);
  }
  serialLineLength = 0U;
}

void serviceBoundedSerial() {
  uint8_t serviced = 0U;
  while (Serial.available() > 0 && serviced < kSerialBytesPerLoop) {
    ++serviced;
    const char input = static_cast<char>(Serial.read());
    if (input == '\r') {
      continue;
    }
    if (input == '\n') {
      if (serialLineLength != 0U) {
        processLine();
      }
      continue;
    }
    if (serialLineLength < sizeof(serialLine) - 2U) {
      serialLine[serialLineLength++] = input;
    } else {
      serialLineLength = 0U;
      Serial.println("!REJECT reason=COMMAND_TOO_LONG");
    }
  }
}

void applyAngleSettleDeadband() {
  if (motor1.controller != MotionControlType::angle ||
      fabsf(motor1.target - motor1.shaft_angle) > kMotorHallSectorAngle) {
    return;
  }
  motor1.current_sp = 0.0F;
  motor1.shaft_velocity_sp = 0.0F;
  motor1.PID_velocity.reset();
}

void serviceControl() {
  const uint32_t nowUs = micros();
  if (previousControlUs != 0U) {
    const uint32_t periodUs = nowUs - previousControlUs;
    maximumControlPeriodUs = max(maximumControlPeriodUs, periodUs);
  }
  previousControlUs = nowUs;

  if (sessionState != TuningSessionState::Armed) {
    holdMotor0PwmLow();
    return;
  }

  motor1.loopFOC();
  motor1.move();
  applyAngleSettleDeadband();
  holdMotor0PwmLow();

  const bool feedbackFinite = isfinite(motor1.current.q) &&
                              isfinite(motor1.current.d) &&
                              isfinite(motor1.shaft_velocity);
  const float currentPeak =
      fmaxf(fabsf(motor1.current.q), fabsf(motor1.current.d));
  if (!feedbackFinite || currentPeak >= kTuningCurrentCeiling ||
      hallSensor.electric_sector < 0 || invalidHallTransitions != 0U) {
    const char* reason = "ILLEGAL_HALL_TRANSITION";
    if (!feedbackFinite) {
      reason = "NONFINITE_FEEDBACK";
    } else if (currentPeak >= kTuningCurrentCeiling) {
      reason = "GROSS_CURRENT_CUTOFF";
    } else if (hallSensor.electric_sector < 0) {
      reason = "ILLEGAL_HALL_STATE";
    }
    disarm(TuningSessionState::Fault, reason);
  }
}

}  // namespace

void setup() {
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
  hallSensor.enableInterrupts(hallA, hallB, hallC);

  Serial.begin(kSerialBaud);
  delay(100);

  motor1Driver.voltage_power_supply = kSupplyVoltage;
  motor1Driver.voltage_limit = kTuningVoltageCeiling;
  driverInitialized = motor1Driver.init();
  motor1Driver.disable();

  motor1.linkSensor(&hallSensor);
  motor1.linkDriver(&motor1Driver);
  currentSense.linkDriver(&motor1Driver);
  if (driverInitialized) {
    currentSenseInitialized = currentSense.init();
  }
  motor1.linkCurrentSense(&currentSense);

  motor1.phase_resistance = 0.545F;
  motor1.phase_inductance = 0.000265F;
  motor1.axis_inductance = {0.000210F, 0.000265F};
  motor1.PID_current_q.P = 0.166504F;
  motor1.PID_current_q.I = 342.434F;
  motor1.PID_current_d.P = 0.131947F;
  motor1.PID_current_d.I = 342.434F;
  motor1.PID_current_q.output_ramp = kDefaultCurrentOutputRamp;
  motor1.PID_current_d.output_ramp = kDefaultCurrentOutputRamp;
  motor1.LPF_current_q.Tf = 0.000318F;
  motor1.LPF_current_d.Tf = 0.000318F;
  motor1.PID_velocity.P = 0.03F;
  motor1.PID_velocity.I = 0.05F;
  motor1.PID_velocity.D = 0.0F;
  motor1.PID_velocity.output_ramp = 30.0F;
  motor1.LPF_velocity.Tf = 0.02F;
  motor1.P_angle.P = 10.0F;
  motor1.updateTorqueControlType(TorqueControlType::foc_current);
  motor1.updateMotionControlType(MotionControlType::velocity);
  motor1.updateVoltageLimit(kDefaultVoltageLimit);
  motor1.updateCurrentLimit(kDefaultCurrentLimit);
  motor1.updateVelocityLimit(kDefaultVelocityLimit);
  motor1.voltage_sensor_align = kAlignmentVoltage;
  motor1.feed_forward_current = {0.0F, 0.0F};
  motor1.feed_forward_voltage = {0.0F, 0.0F};
  motor1.useMonitoring(Serial);
  // Studio recognizes telemetry only when each line starts with a number.
  // WebController sends @3 during connection; processLine() then adds the
  // motor-id framing that its graph parser requires.
  motor1.monitor_start_char = '\0';
  motor1.monitor_end_char = '\0';
  motor1.monitor_variables = 0U;
  motor1.monitor_downsample = 0U;

  commander.add('M', motorAdapter, "motor1-development-tuning");
  // Studio parses the stock human-readable Commander responses. WebController
  // sends @3 on every connection and switches this to machine-readable before
  // discovery, so both clients retain their native upstream protocol.
  commander.verbose = VerboseMode::user_friendly;
  commandAllPwmOff();

  Serial.printf(
      "!BOOT app=motor-tuning reset_reason=%d state=MOTIONLESS "
      "pwm_state=OFF driver_init=%s current_sense_init=%s "
      "commands=XALIGN,XSTATUS arm_control=ME1 disarm_control=ME0\n",
      static_cast<int>(esp_reset_reason()),
      driverInitialized ? "PASS" : "FAIL",
      currentSenseInitialized ? "PASS" : "FAIL");
}

void loop() {
  serviceControl();
  serviceBoundedSerial();
  if (motor1.monitor_variables != 0U && motor1.monitor_downsample != 0U) {
    motor1.monitor();
  }
}
