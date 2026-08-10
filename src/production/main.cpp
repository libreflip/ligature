#include <Arduino.h>
#include <SimpleFOC.h>
#include <math.h>

#include "production/commissioned_config.h"
#include "production/controller.h"
#include "production/fixed_hall_sensor.h"
#include "serial_baud.h"

namespace {

using ligature::Controller;
using ligature::DriveMode;
using ligature::DriveRequest;
using ligature::LineSink;
using ligature::Observation;

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
constexpr float kSupplyVoltage = 24.0F;
constexpr float kNominalShuntOhms = 0.01F;
constexpr float kNominalAmplifierGain = 50.0F;
constexpr uint8_t kSerialBytesPerLoop = 8U;

#ifndef LIGATURE_ENDSTOP_PIN
constexpr int8_t kEndstopPin = -1;
#else
constexpr int8_t kEndstopPin = LIGATURE_ENDSTOP_PIN;
#endif

// Candidate values from geared-motor commissioning. They do not make the
// assembled lift-axis configuration commissioned; see commissioned_config.h.
constexpr float kStoredElectricalZero = 5.235988F;

BLDCDriver3PWM driver(kMotor1PwmA, kMotor1PwmB, kMotor1PwmC);
BLDCMotor motor(kMotorPolePairs);
FixedHallSensor hall(kHallPinA, kHallPinB, kHallPinC, kMotorPolePairs);
InlineCurrentSense currentSense(kNominalShuntOhms, kNominalAmplifierGain,
                                kMotor1CurrentA, kMotor1CurrentB);

class SerialSink : public LineSink {
 public:
  void writeLine(const char* line) override { Serial.println(line); }
};

SerialSink serialSink;
Controller controller(ligature::kCommissionedConfig, serialSink);
bool driverInitialized = false;
bool motorInitialized = false;
bool currentSenseInitialized = false;
bool motorActive = false;
char lineBuffer[ligature::kMaximumProductionLine + 1U] = {};
size_t lineLength = 0U;
bool discardingLine = false;
uint32_t previousLoopUs = 0U;
bool controlDeadlineMissed = false;

void holdUnusedChannelLow() {
  digitalWrite(kMotor0PwmA, LOW);
  digitalWrite(kMotor0PwmB, LOW);
  digitalWrite(kMotor0PwmC, LOW);
}

void allPwmOff() {
  motor.target = motor.shaft_angle;
  motor.current_sp = 0.0F;
  if (motorInitialized) motor.disable();
  if (driverInitialized) driver.disable();
  motorActive = false;
  holdUnusedChannelLow();
}

void IRAM_ATTR hallA() { hall.handleA(); }
void IRAM_ATTR hallB() { hall.handleB(); }
void IRAM_ATTR hallC() { hall.handleC(); }

Observation observe() {
  return {
      millis(),
      motor.shaft_angle,
      motor.shaft_velocity,
      motor.current.q,
      kEndstopPin >= 0 ? digitalRead(kEndstopPin) == LOW : false,
      hall.sequenceValid() && hall.electricSector() >= 0,
      isfinite(motor.current.q) && isfinite(motor.current.d),
      false,  // ADC clipping awaits the final current-fault policy/adapter.
      controlDeadlineMissed,
  };
}

void enableMotorAtCurrentAngle() {
  if (!motorInitialized || motorActive) return;
  motor.target = motor.shaft_angle;
  motor.current_sp = 0.0F;
  motor.PID_velocity.reset();
  motor.PID_current_q.reset();
  motor.PID_current_d.reset();
  motor.enable();
  motorActive = true;
}

void applyDriveRequest(const DriveRequest& request) {
  switch (request.mode) {
    case DriveMode::PwmOff:
      allPwmOff();
      return;
    case DriveMode::Alignment:
      // Production alignment must be cooperative so M112 can interrupt every
      // phase. The baseline exposes the core seam but fails this operation
      // closed rather than calling blocking SimpleFOC initFOC().
      allPwmOff();
      controller.alignmentComplete(false, 0.0F, "UNKNOWN", observe());
      return;
    case DriveMode::HoldAngle:
    case DriveMode::Position:
      motor.updateTorqueControlType(TorqueControlType::foc_current);
      motor.updateMotionControlType(MotionControlType::angle);
      motor.updateCurrentLimit(request.currentLimitA);
      motor.updateVelocityLimit(request.speedLimitRadS > 0.0F
                                    ? request.speedLimitRadS
                                    : motor.velocity_limit);
      motor.target = request.target;
      enableMotorAtCurrentAngle();
      motor.target = request.target;
      return;
    case DriveMode::Velocity:
      motor.updateTorqueControlType(TorqueControlType::foc_current);
      motor.updateMotionControlType(MotionControlType::velocity);
      motor.updateCurrentLimit(request.currentLimitA);
      motor.target = request.target;
      enableMotorAtCurrentAngle();
      motor.target = request.target;
      return;
    case DriveMode::Torque:
      motor.updateTorqueControlType(TorqueControlType::foc_current);
      motor.updateMotionControlType(MotionControlType::torque);
      motor.updateCurrentLimit(request.currentLimitA);
      motor.target = request.target;
      enableMotorAtCurrentAngle();
      motor.target = request.target;
      return;
  }
}

void processLine() {
  controller.acceptLine(lineBuffer, lineLength, observe());
  lineLength = 0U;
}

void serviceBoundedSerial() {
  uint8_t serviced = 0U;
  while (Serial.available() > 0 && serviced < kSerialBytesPerLoop) {
    ++serviced;
    const char input = static_cast<char>(Serial.read());
    if (input == '\n') {
      if (discardingLine) {
        Serial.println("error LINE REASON:LINE_TOO_LONG");
        discardingLine = false;
        lineLength = 0U;
      } else {
        processLine();
      }
      continue;
    }
    if (discardingLine) continue;
    if (lineLength < ligature::kMaximumProductionLine) {
      lineBuffer[lineLength++] = input;
    } else {
      discardingLine = true;
    }
  }
}

void configureMotor() {
  motor.linkSensor(&hall);
  motor.linkDriver(&driver);
  motor.linkCurrentSense(&currentSense);
  motor.phase_resistance = 0.545F;
  motor.phase_inductance = 0.000265F;
  motor.axis_inductance = {0.000210F, 0.000265F};
  motor.PID_current_q.P = 0.166504F;
  motor.PID_current_q.I = 342.434F;
  motor.PID_current_d.P = 0.131947F;
  motor.PID_current_d.I = 342.434F;
  motor.PID_current_q.output_ramp = 10000.0F;
  motor.PID_current_d.output_ramp = 10000.0F;
  motor.LPF_current_q.Tf = 0.000318F;
  motor.LPF_current_d.Tf = 0.000318F;
  motor.PID_velocity.P = 0.03F;
  motor.PID_velocity.I = 0.05F;
  motor.PID_velocity.D = 0.0F;
  motor.PID_velocity.output_ramp = 30.0F;
  motor.LPF_velocity.Tf = 0.02F;
  motor.P_angle.P = 10.0F;
  motor.P_angle.output_ramp = 100.0F;
  motor.updateTorqueControlType(TorqueControlType::foc_current);
  motor.updateMotionControlType(MotionControlType::angle);
  motor.updateVoltageLimit(ligature::kCommissionedConfig.voltageLimitV);
  motor.updateCurrentLimit(ligature::kCommissionedConfig.currentLimitA);
  motor.updateVelocityLimit(100.0F);
  motor.feed_forward_current = {0.0F, 0.0F};
  motor.feed_forward_voltage = {0.0F, 0.0F};
  motor.sensor_direction = Direction::CCW;
  motor.zero_electric_angle = kStoredElectricalZero;
  currentSense.skip_align = true;
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
  if (kEndstopPin >= 0) pinMode(kEndstopPin, INPUT_PULLUP);

  hall.pullup = Pullup::USE_EXTERN;
  hall.init();
  hall.enableInterrupts(hallA, hallB, hallC);

  Serial.begin(kSerialBaud);
  delay(100);

  driver.voltage_power_supply = kSupplyVoltage;
  driver.voltage_limit = ligature::kCommissionedConfig.maximumVoltageV;
  driverInitialized = driver.init();
  driver.disable();
  currentSense.linkDriver(&driver);
  if (driverInitialized) currentSenseInitialized = currentSense.init();
  configureMotor();

  // BLDCMotor::init()/initFOC() are intentionally skipped while the compiled
  // configuration is uncommissioned, so boot cannot align or move.
  if (ligature::kCommissionedConfig.commissioned && driverInitialized &&
      currentSenseInitialized) {
    motorInitialized = motor.init() != 0;
    motor.disable();
    if (motorInitialized) motorInitialized = motor.initFOC() != 0;
    motor.disable();
  }
  allPwmOff();
  controller.reset(observe(), currentSenseInitialized);
  Serial.printf(
      "boot APP:production STATE:%s PWM:OFF COMMISSIONED:%d "
      "DRIVER_INIT:%d CURRENT_SENSE_INIT:%d ENDSTOP_CONFIGURED:%d\n",
      controller.state() == ligature::State::CommissioningOnly
          ? "COMMISSIONING_ONLY"
          : "IDLE",
      ligature::kCommissionedConfig.commissioned ? 1 : 0,
      driverInitialized ? 1 : 0, currentSenseInitialized ? 1 : 0,
      kEndstopPin >= 0 ? 1 : 0);
}

void loop() {
  const uint32_t nowUs = micros();
  if (previousLoopUs != 0U) {
    // This conservative adapter signal is disabled until the assembled-axis
    // timing profile supplies a commissioned threshold.
    controlDeadlineMissed = false;
  }
  previousLoopUs = nowUs;

  const DriveRequest before = controller.driveRequest();
  applyDriveRequest(before);
  if (motorActive) {
    motor.loopFOC();
    motor.move();
  }
  holdUnusedChannelLow();
  controller.tick(observe());
  applyDriveRequest(controller.driveRequest());
  serviceBoundedSerial();
}
