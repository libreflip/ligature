#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>

// Project-owned Hall sensor equivalent to SimpleFOC 2.4.0's HallSensor with
// unsigned timer arithmetic, stale-period clearing, and sticky legal-sequence
// validation. This incorporates the behavior required by upstream PR #469
// without patching PlatformIO's dependency cache.
class FixedHallSensor : public Sensor {
 public:
  FixedHallSensor(int pinA, int pinB, int pinC, int polePairs);

  void init();
  void enableInterrupts(void (*handleA)(), void (*handleB)(),
                        void (*handleC)());
  void handleA();
  void handleB();
  void handleC();
  void update() override;
  float getSensorAngle() override;
  float getVelocity() override;

  bool sequenceValid() const { return sequenceValid_; }
  int8_t electricSector() const { return electricSector_; }

  Pullup pullup = Pullup::USE_EXTERN;
  Direction direction = Direction::CW;

 private:
  void updateState();
  static int8_t sectorForState(uint8_t state);

  int pinA_;
  int pinB_;
  int pinC_;
  int cpr_;
  bool useInterrupts_ = false;
  volatile uint8_t hallState_ = 0U;
  volatile int8_t electricSector_ = -1;
  volatile int32_t electricRotations_ = 0;
  volatile uint32_t pulseTimestampUs_ = 0U;
  volatile uint32_t pulsePeriodUs_ = 0U;
  volatile bool sequenceValid_ = true;
  volatile int activeA_ = 0;
  volatile int activeB_ = 0;
  volatile int activeC_ = 0;
  Direction oldDirection_ = Direction::CW;
};
