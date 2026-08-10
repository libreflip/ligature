#include "production/fixed_hall_sensor.h"

FixedHallSensor::FixedHallSensor(const int pinA, const int pinB, const int pinC,
                                 const int polePairs)
    : pinA_(pinA), pinB_(pinB), pinC_(pinC), cpr_(polePairs * 6) {}

int8_t FixedHallSensor::sectorForState(const uint8_t state) {
  constexpr int8_t kSectors[8] = {-1, 0, 4, 5, 2, 1, 3, -1};
  return kSectors[state & 7U];
}

void FixedHallSensor::init() {
  pinMode(pinA_, pullup == Pullup::USE_INTERN ? INPUT_PULLUP : INPUT);
  pinMode(pinB_, pullup == Pullup::USE_INTERN ? INPUT_PULLUP : INPUT);
  pinMode(pinC_, pullup == Pullup::USE_INTERN ? INPUT_PULLUP : INPUT);
  activeA_ = digitalRead(pinA_);
  activeB_ = digitalRead(pinB_);
  activeC_ = digitalRead(pinC_);
  hallState_ = static_cast<uint8_t>(activeC_ + (activeB_ << 1) +
                                    (activeA_ << 2));
  electricSector_ = sectorForState(hallState_);
  electricRotations_ = 0;
  pulsePeriodUs_ = 0U;
  pulseTimestampUs_ = micros();
  sequenceValid_ = electricSector_ >= 0;
}

void FixedHallSensor::enableInterrupts(void (*handleA)(), void (*handleB)(),
                                      void (*handleC)()) {
  if (handleA != nullptr)
    attachInterrupt(digitalPinToInterrupt(pinA_), handleA, CHANGE);
  if (handleB != nullptr)
    attachInterrupt(digitalPinToInterrupt(pinB_), handleB, CHANGE);
  if (handleC != nullptr)
    attachInterrupt(digitalPinToInterrupt(pinC_), handleC, CHANGE);
  useInterrupts_ = true;
}

void FixedHallSensor::handleA() {
  activeA_ = digitalRead(pinA_);
  updateState();
}
void FixedHallSensor::handleB() {
  activeB_ = digitalRead(pinB_);
  updateState();
}
void FixedHallSensor::handleC() {
  activeC_ = digitalRead(pinC_);
  updateState();
}

void FixedHallSensor::updateState() {
  const uint8_t newState = static_cast<uint8_t>(
      activeC_ + (activeB_ << 1) + (activeA_ << 2));
  if (newState == hallState_) return;
  const int8_t newSector = sectorForState(newState);
  const int8_t oldSector = electricSector_;
  hallState_ = newState;
  if (newSector < 0 || oldSector < 0) {
    sequenceValid_ = false;
    return;
  }
  const uint8_t delta = static_cast<uint8_t>((newSector - oldSector + 6) % 6);
  if (delta != 1U && delta != 5U) {
    sequenceValid_ = false;
    return;
  }

  const Direction newDirection = delta == 1U ? Direction::CW : Direction::CCW;
  if (oldSector == 5 && newSector == 0) ++electricRotations_;
  if (oldSector == 0 && newSector == 5) --electricRotations_;
  electricSector_ = newSector;
  const uint32_t now = micros();
  pulsePeriodUs_ = newDirection == oldDirection_
                       ? static_cast<uint32_t>(now - pulseTimestampUs_)
                       : 0U;
  pulseTimestampUs_ = now;
  direction = newDirection;
  oldDirection_ = newDirection;
}

void FixedHallSensor::update() {
  if (useInterrupts_) {
    noInterrupts();
  } else {
    activeA_ = digitalRead(pinA_);
    activeB_ = digitalRead(pinB_);
    activeC_ = digitalRead(pinC_);
    updateState();
  }
  const uint32_t timestamp = pulseTimestampUs_;
  const int32_t rotations = electricRotations_;
  const int8_t sector = electricSector_;
  if (useInterrupts_) interrupts();
  angle_prev_ts = timestamp;
  angle_prev = static_cast<float>((rotations * 6 + sector) % cpr_) /
               static_cast<float>(cpr_) * _2PI;
  full_rotations = (rotations * 6 + sector) / cpr_;
}

float FixedHallSensor::getSensorAngle() {
  noInterrupts();
  const int32_t rotations = electricRotations_;
  const int8_t sector = electricSector_;
  interrupts();
  return static_cast<float>(rotations * 6 + sector) /
         static_cast<float>(cpr_) * _2PI;
}

float FixedHallSensor::getVelocity() {
  noInterrupts();
  const uint32_t timestamp = pulseTimestampUs_;
  uint32_t period = pulsePeriodUs_;
  const Direction sampledDirection = direction;
  interrupts();
  const uint32_t age = static_cast<uint32_t>(micros() - timestamp);
  if (period == 0U || (age > period && age - period > period)) {
    noInterrupts();
    pulsePeriodUs_ = 0U;
    interrupts();
    return 0.0F;
  }
  const float sign = sampledDirection == Direction::CW ? 1.0F : -1.0F;
  return sign * (_2PI / static_cast<float>(cpr_)) /
         (static_cast<float>(period) / 1000000.0F);
}
