#include <Arduino.h>
#include <soc/gpio_struct.h>

#ifndef HALL_USE_SIMPLEFOC
#define HALL_USE_SIMPLEFOC 0
#endif

#if HALL_USE_SIMPLEFOC
#include <SimpleFOC.h>
#endif

#ifndef HALL_SERIAL_BAUD
#define HALL_SERIAL_BAUD 460800UL
#endif

namespace {

// MKS ESP32 FOC V1.0 ENCODER_1 header. Keep the physical Hall labels intact;
// phase/Hall remapping belongs to later motor-control work.
constexpr uint8_t kHallPinA = 23;  // SDA_1
constexpr uint8_t kHallPinB = 5;   // SCL_1
constexpr uint8_t kHallPinC = 13;  // I_1

// MKS ESP32 FOC V1.0 phase-control nets. The schematic has no separate
// bridge-enable input, so this diagnostic holds all six controls low. This is
// a software PWM-off state, not power isolation.
constexpr uint8_t kMotor0PwmA = 32;
constexpr uint8_t kMotor0PwmB = 33;
constexpr uint8_t kMotor0PwmC = 25;
constexpr uint8_t kMotor1PwmA = 26;
constexpr uint8_t kMotor1PwmB = 27;
constexpr uint8_t kMotor1PwmC = 14;

constexpr uint8_t kMotorPolePairs = 4;
constexpr uint16_t kTransitionsPerMotorRevolution = 24;
constexpr uint32_t kHeartbeatIntervalMs = 500;

#if HALL_USE_SIMPLEFOC
constexpr float kTwoPi = 2.0F * PI;
constexpr float kMechanicalStepRad =
    kTwoPi / static_cast<float>(kTransitionsPerMotorRevolution);
constexpr float kAngleToleranceRad = kMechanicalStepRad / 2.0F;
constexpr uint32_t kSimpleFocDumpIntervalMs = 50;

HallSensor hallSensor(kHallPinA, kHallPinB, kHallPinC, kMotorPolePairs);
#endif

struct HallEvent {
  uint32_t timestampUs;
  uint8_t state;
#if HALL_USE_SIMPLEFOC
  int32_t simpleFocStep;
  int32_t simpleFocInterrupts;
#endif
};

constexpr uint16_t kEventQueueSize = 128;
constexpr uint16_t kEventQueueMask = kEventQueueSize - 1;
static_assert((kEventQueueSize & kEventQueueMask) == 0,
              "Hall event queue size must be a power of two");

volatile HallEvent eventQueue[kEventQueueSize];
volatile uint16_t eventHead = 0;
volatile uint16_t eventTail = 0;
volatile uint32_t droppedEvents = 0;

struct CaptureStats {
  bool active = false;
  bool hasResult = false;
  bool rawPassed = false;
  bool passed = false;
  uint8_t startState = 0;
  uint8_t previousState = 0;
  uint8_t finalState = 0;
  uint8_t visitedStates = 0;
  uint8_t observedSequence[6] = {};
  uint8_t observedSequenceLength = 0;
  int8_t direction = 0;
  uint16_t validTransitions = 0;
  uint16_t rawTransitions = 0;
  uint16_t invalidStates = 0;
  uint16_t invalidTransitions = 0;
  uint16_t duplicateInterrupts = 0;
  uint16_t reversals = 0;
  uint32_t startedAtUs = 0;
  uint32_t durationUs = 0;
  uint32_t droppedAtStart = 0;
  uint32_t droppedDuringCapture = 0;
#if HALL_USE_SIMPLEFOC
  bool simpleFocPassed = false;
  int8_t simpleFocDirection = 0;
  int32_t simpleFocStartStep = 0;
  int32_t simpleFocPreviousStep = 0;
  int32_t simpleFocFinalStep = 0;
  int32_t simpleFocInterruptsAtStart = 0;
  int32_t simpleFocInterrupts = 0;
  uint16_t simpleFocSteps = 0;
  uint16_t simpleFocZeroSteps = 0;
  uint16_t simpleFocInvalidSteps = 0;
  uint16_t simpleFocReversals = 0;
  float simpleFocStartAngleRad = 0.0F;
  float simpleFocFinalAngleRad = 0.0F;
  float simpleFocDeltaAngleRad = 0.0F;
#endif
};

CaptureStats capture;
uint8_t currentState = 0;
uint32_t liveEdges = 0;
uint32_t lastHeartbeatMs = 0;
#if HALL_USE_SIMPLEFOC
bool simpleFocDumpEnabled = false;
uint32_t lastSimpleFocDumpMs = 0;
#endif

uint8_t IRAM_ATTR readHallStateFast() {
  const uint32_t levels = GPIO.in;
  return static_cast<uint8_t>((((levels >> kHallPinA) & 1U) << 2U) |
                              (((levels >> kHallPinB) & 1U) << 1U) |
                              ((levels >> kHallPinC) & 1U));
}

void IRAM_ATTR queueHallEvent() {
  const uint16_t head = eventHead;
  const uint16_t next = (head + 1U) & kEventQueueMask;

  if (next == eventTail) {
    droppedEvents = droppedEvents + 1U;
    return;
  }

  eventQueue[head].timestampUs = micros();
  eventQueue[head].state = readHallStateFast();
#if HALL_USE_SIMPLEFOC
  // Snapshot the library decoder immediately after handleA/B/C() so queued
  // serial output cannot blur multiple Hall steps into one observation.
  eventQueue[head].simpleFocStep =
      static_cast<int32_t>(hallSensor.electric_rotations) * 6L +
      static_cast<int32_t>(hallSensor.electric_sector);
  eventQueue[head].simpleFocInterrupts =
      static_cast<int32_t>(hallSensor.total_interrupts);
#endif
  eventHead = next;
}

#if HALL_USE_SIMPLEFOC
void IRAM_ATTR handleHallEdgeA() {
  hallSensor.handleA();
  queueHallEvent();
}

void IRAM_ATTR handleHallEdgeB() {
  hallSensor.handleB();
  queueHallEvent();
}

void IRAM_ATTR handleHallEdgeC() {
  hallSensor.handleC();
  queueHallEvent();
}
#else
void IRAM_ATTR handleHallEdge() {
  queueHallEvent();
}
#endif

bool popEvent(HallEvent& event) {
  const uint16_t tail = eventTail;
  if (tail == eventHead) {
    return false;
  }

  event.timestampUs = eventQueue[tail].timestampUs;
  event.state = eventQueue[tail].state;
#if HALL_USE_SIMPLEFOC
  event.simpleFocStep = eventQueue[tail].simpleFocStep;
  event.simpleFocInterrupts = eventQueue[tail].simpleFocInterrupts;
#endif
  eventTail = (tail + 1U) & kEventQueueMask;
  return true;
}

bool isValidHallState(const uint8_t state) {
  return state != 0U && state != 7U;
}

bool changesExactlyOneBit(const uint8_t changedBits) {
  return changedBits != 0U &&
         (changedBits & static_cast<uint8_t>(changedBits - 1U)) == 0U;
}

int8_t canonicalStateIndex(const uint8_t state) {
  // One valid six-state Gray-code cycle. A different starting state only
  // rotates this table; swapping physical Hall labels reverses its direction.
  constexpr uint8_t sequence[] = {1, 5, 4, 6, 2, 3};
  for (uint8_t index = 0; index < 6; ++index) {
    if (sequence[index] == state) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

int8_t transitionDirection(const uint8_t from, const uint8_t to) {
  const int8_t fromIndex = canonicalStateIndex(from);
  const int8_t toIndex = canonicalStateIndex(to);
  if (fromIndex < 0 || toIndex < 0) {
    return 0;
  }

  const uint8_t delta =
      static_cast<uint8_t>((toIndex - fromIndex + 6) % 6);
  if (delta == 1U) {
    return 1;
  }
  if (delta == 5U) {
    return -1;
  }
  return 0;
}

const char* directionName(const int8_t direction) {
  if (direction > 0) {
    return "FORWARD_SEQUENCE";
  }
  if (direction < 0) {
    return "REVERSE_SEQUENCE";
  }
  return "UNKNOWN";
}

#if HALL_USE_SIMPLEFOC
const char* signName(const int8_t direction) {
  if (direction > 0) {
    return "POSITIVE";
  }
  if (direction < 0) {
    return "NEGATIVE";
  }
  return "UNKNOWN";
}

void readSimpleFocCounters(int32_t& step, int32_t& interruptCount) {
  noInterrupts();
  step = static_cast<int32_t>(hallSensor.electric_rotations) * 6L +
         static_cast<int32_t>(hallSensor.electric_sector);
  interruptCount = static_cast<int32_t>(hallSensor.total_interrupts);
  interrupts();
}

float simpleFocAngleForStep(const int32_t step) {
  return static_cast<float>(step) * kMechanicalStepRad;
}
#endif

void printHallState(const uint8_t state) {
  Serial.print((state >> 2U) & 1U);
  Serial.print((state >> 1U) & 1U);
  Serial.print(state & 1U);
}

uint8_t validStateCount(const uint8_t states) {
  uint8_t count = 0;
  for (uint8_t state = 1; state < 7; ++state) {
    if ((states & (1U << state)) != 0U) {
      ++count;
    }
  }
  return count;
}

void printObservedSequence(const bool reverse) {
  if (capture.observedSequenceLength != 6U) {
    Serial.print("UNKNOWN");
    return;
  }

  for (uint8_t position = 0; position < 6; ++position) {
    if (position != 0U) {
      Serial.print('-');
    }

    const uint8_t sequenceIndex =
        reverse && position != 0U ? static_cast<uint8_t>(6U - position)
                                  : position;
    printHallState(capture.observedSequence[sequenceIndex]);
  }
}

void printCaptureSummary(const char* recordType) {
  Serial.printf(
      "%s status=%s edges=%u raw_edges=%u states=%u invalid_states=%u "
      "invalid_transitions=%u duplicates=%u reversals=%u dropped=%lu "
      "direction=%s duration_us=%lu start=",
      recordType, capture.passed ? "PASS" : "FAIL",
      capture.validTransitions, capture.rawTransitions,
      validStateCount(capture.visitedStates), capture.invalidStates,
      capture.invalidTransitions, capture.duplicateInterrupts,
      capture.reversals,
      static_cast<unsigned long>(capture.droppedDuringCapture),
      directionName(capture.direction),
      static_cast<unsigned long>(capture.durationUs));
  printHallState(capture.startState);
  Serial.print(" final=");
  printHallState(capture.finalState);
  Serial.println();
  Serial.print("SEQUENCE observed=");
  printObservedSequence(false);
  Serial.print(" reverse=");
  printObservedSequence(true);
  Serial.println();
#if HALL_USE_SIMPLEFOC
  Serial.printf(
      "SIMPLEFOC status=%s interrupts=%ld steps=%u zero_steps=%u "
      "invalid_steps=%u reversals=%u sign=%s start_step=%ld final_step=%ld "
      "start_angle_rad=%.6f final_angle_rad=%.6f delta_angle_rad=%.6f "
      "target_abs_rad=%.6f tolerance_rad=%.6f raw_status=%s\n",
      capture.simpleFocPassed ? "PASS" : "FAIL",
      static_cast<long>(capture.simpleFocInterrupts),
      capture.simpleFocSteps, capture.simpleFocZeroSteps,
      capture.simpleFocInvalidSteps, capture.simpleFocReversals,
      signName(capture.simpleFocDirection),
      static_cast<long>(capture.simpleFocStartStep),
      static_cast<long>(capture.simpleFocFinalStep),
      capture.simpleFocStartAngleRad, capture.simpleFocFinalAngleRad,
      capture.simpleFocDeltaAngleRad, kTwoPi, kAngleToleranceRad,
      capture.rawPassed ? "PASS" : "FAIL");
#endif
}

void finishCapture(const uint32_t finishedAtUs) {
  capture.active = false;
  capture.hasResult = true;
  capture.finalState = capture.previousState;
  capture.durationUs = finishedAtUs - capture.startedAtUs;
  capture.droppedDuringCapture = droppedEvents - capture.droppedAtStart;

  capture.rawPassed =
      isValidHallState(capture.startState) &&
      capture.validTransitions == kTransitionsPerMotorRevolution &&
      capture.rawTransitions == kTransitionsPerMotorRevolution &&
      validStateCount(capture.visitedStates) == 6U &&
      capture.invalidStates == 0U && capture.invalidTransitions == 0U &&
      capture.duplicateInterrupts == 0U && capture.reversals == 0U &&
      capture.droppedDuringCapture == 0U && capture.direction != 0 &&
      capture.finalState == capture.startState;

#if HALL_USE_SIMPLEFOC
  capture.simpleFocFinalAngleRad =
      simpleFocAngleForStep(capture.simpleFocFinalStep);
  capture.simpleFocDeltaAngleRad =
      capture.simpleFocFinalAngleRad - capture.simpleFocStartAngleRad;
  const float angleError =
      fabsf(fabsf(capture.simpleFocDeltaAngleRad) - kTwoPi);
  const bool angleDirectionMatches =
      (capture.simpleFocDirection > 0 &&
       capture.simpleFocDeltaAngleRad > 0.0F) ||
      (capture.simpleFocDirection < 0 &&
       capture.simpleFocDeltaAngleRad < 0.0F);

  capture.simpleFocPassed =
      capture.simpleFocInterrupts == kTransitionsPerMotorRevolution &&
      capture.simpleFocSteps == kTransitionsPerMotorRevolution &&
      capture.simpleFocZeroSteps == 0U &&
      capture.simpleFocInvalidSteps == 0U &&
      capture.simpleFocReversals == 0U &&
      capture.simpleFocDirection != 0 && angleDirectionMatches &&
      angleError <= kAngleToleranceRad;
  capture.passed = capture.rawPassed && capture.simpleFocPassed;
#else
  capture.passed = capture.rawPassed;
#endif

  printCaptureSummary("RESULT");
  Serial.println(
      "CHECK shaft_mark_returned=REQUIRED expected_motor_revolutions=1");
}

void startCapture() {
  capture = CaptureStats{};
  capture.active = true;
  capture.startState = readHallStateFast();
  capture.previousState = capture.startState;
  capture.finalState = capture.startState;
  capture.visitedStates = static_cast<uint8_t>(1U << capture.startState);
  if (isValidHallState(capture.startState)) {
    capture.observedSequence[0] = capture.startState;
    capture.observedSequenceLength = 1;
  }
  capture.invalidStates = isValidHallState(capture.startState) ? 0U : 1U;
  capture.startedAtUs = micros();
  capture.droppedAtStart = droppedEvents;

#if HALL_USE_SIMPLEFOC
  hallSensor.update();
  readSimpleFocCounters(capture.simpleFocStartStep,
                        capture.simpleFocInterruptsAtStart);
  capture.simpleFocPreviousStep = capture.simpleFocStartStep;
  capture.simpleFocFinalStep = capture.simpleFocStartStep;
  capture.simpleFocStartAngleRad =
      simpleFocAngleForStep(capture.simpleFocStartStep);
  capture.simpleFocFinalAngleRad = capture.simpleFocStartAngleRad;
#endif

  Serial.print("TEST status=START target_edges=");
  Serial.print(kTransitionsPerMotorRevolution);
  Serial.print(" start=");
  printHallState(capture.startState);
  Serial.println(" instruction=TURN_SHAFT_ONE_DIRECTION");
}

void updateCapture(const HallEvent& event) {
  if (!capture.active) {
    return;
  }

  if (event.state == capture.previousState) {
    ++capture.duplicateInterrupts;
    return;
  }

  ++capture.rawTransitions;
  capture.visitedStates |= static_cast<uint8_t>(1U << event.state);

#if HALL_USE_SIMPLEFOC
  const int32_t simpleFocStepDelta =
      event.simpleFocStep - capture.simpleFocPreviousStep;
  if (simpleFocStepDelta == 0) {
    ++capture.simpleFocZeroSteps;
  } else if (simpleFocStepDelta != 1 && simpleFocStepDelta != -1) {
    ++capture.simpleFocInvalidSteps;
  } else {
    ++capture.simpleFocSteps;
    const int8_t simpleFocEdgeDirection =
        simpleFocStepDelta > 0 ? 1 : -1;
    if (capture.simpleFocDirection == 0) {
      capture.simpleFocDirection = simpleFocEdgeDirection;
    } else if (capture.simpleFocDirection != simpleFocEdgeDirection) {
      ++capture.simpleFocReversals;
    }
  }
  capture.simpleFocPreviousStep = event.simpleFocStep;
  capture.simpleFocFinalStep = event.simpleFocStep;
  capture.simpleFocInterrupts =
      event.simpleFocInterrupts - capture.simpleFocInterruptsAtStart;
#endif

  const uint8_t changedBits = capture.previousState ^ event.state;
  const bool validStates = isValidHallState(capture.previousState) &&
                           isValidHallState(event.state);
  const bool singleBit = changesExactlyOneBit(changedBits);
  const int8_t edgeDirection =
      validStates && singleBit
          ? transitionDirection(capture.previousState, event.state)
          : 0;

  if (!isValidHallState(event.state)) {
    ++capture.invalidStates;
  }

  if (!validStates || !singleBit || edgeDirection == 0) {
    ++capture.invalidTransitions;
  } else {
    ++capture.validTransitions;
    if (capture.direction == 0) {
      capture.direction = edgeDirection;
    } else if (capture.direction != edgeDirection) {
      ++capture.reversals;
    }

    const uint8_t stateBit = static_cast<uint8_t>(1U << event.state);
    bool alreadyRecorded = false;
    for (uint8_t index = 0; index < capture.observedSequenceLength; ++index) {
      if (capture.observedSequence[index] == event.state) {
        alreadyRecorded = true;
        break;
      }
    }
    if (!alreadyRecorded && capture.observedSequenceLength < 6U) {
      capture.observedSequence[capture.observedSequenceLength] = event.state;
      ++capture.observedSequenceLength;
    }
    capture.visitedStates |= stateBit;
  }

  capture.previousState = event.state;

  if (capture.validTransitions >= kTransitionsPerMotorRevolution) {
    finishCapture(event.timestampUs);
  }
}

void printLiveEvent(const HallEvent& event) {
  const uint8_t previousState = currentState;
  const uint8_t changedBits = previousState ^ event.state;
  const bool changed = changedBits != 0U;
  if (changed) {
    ++liveEdges;
  }

  Serial.printf("LIVE t_us=%lu abc=",
                static_cast<unsigned long>(event.timestampUs));
  printHallState(event.state);
  Serial.print(" previous=");
  printHallState(previousState);
  Serial.print(" changed=");
  printHallState(changedBits);
  Serial.printf(" valid=%u edge=%lu", isValidHallState(event.state) ? 1 : 0,
                static_cast<unsigned long>(liveEdges));
#if HALL_USE_SIMPLEFOC
  hallSensor.update();
  const int32_t captureStartStep =
      capture.active ? capture.simpleFocStartStep : event.simpleFocStep;
  Serial.printf(
      " sf_step=%ld sf_angle_rad=%.6f sf_capture_delta_rad=%.6f "
      "sf_velocity_rad_s=%.6f",
      static_cast<long>(event.simpleFocStep),
      simpleFocAngleForStep(event.simpleFocStep),
      simpleFocAngleForStep(event.simpleFocStep - captureStartStep),
      hallSensor.getVelocity());
#endif
  Serial.println();

  currentState = event.state;
}

#if HALL_USE_SIMPLEFOC
void printSimpleFocStatusFields() {
  hallSensor.update();
  int32_t step = 0;
  int32_t interruptCount = 0;
  readSimpleFocCounters(step, interruptCount);
  Serial.printf(
      " sf_step=%ld sf_angle_rad=%.6f sf_velocity_rad_s=%.6f dump=%s",
      static_cast<long>(step), hallSensor.getAngle(),
      hallSensor.getVelocity(), simpleFocDumpEnabled ? "ON" : "OFF");
}
#endif

void printStatus() {
  if (capture.active) {
    Serial.printf(
        "STATUS capture=RUNNING edges=%u raw_edges=%u invalid_states=%u "
        "invalid_transitions=%u reversals=%u direction=%s abc=",
        capture.validTransitions, capture.rawTransitions,
        capture.invalidStates, capture.invalidTransitions, capture.reversals,
        directionName(capture.direction));
    printHallState(readHallStateFast());
#if HALL_USE_SIMPLEFOC
    printSimpleFocStatusFields();
#endif
    Serial.println();
    return;
  }

  if (capture.hasResult) {
    printCaptureSummary("SUMMARY");
    return;
  }

  Serial.print("STATUS capture=IDLE result=NONE abc=");
  printHallState(readHallStateFast());
  Serial.printf(" live_edges=%lu dropped=%lu",
                static_cast<unsigned long>(liveEdges),
                static_cast<unsigned long>(droppedEvents));
#if HALL_USE_SIMPLEFOC
  printSimpleFocStatusFields();
#endif
  Serial.println();
}

void processSerialInput() {
  while (Serial.available() > 0) {
    char command = static_cast<char>(Serial.read());
    if (command >= 'a' && command <= 'z') {
      command = static_cast<char>(command - 'a' + 'A');
    }

    if (command == 'R') {
      startCapture();
    } else if (command == 'S' || command == '?') {
      printStatus();
    } else if (command == 'D') {
#if HALL_USE_SIMPLEFOC
      simpleFocDumpEnabled = !simpleFocDumpEnabled;
      lastSimpleFocDumpMs = 0;
      Serial.printf("DUMP status=%s rate_hz=20\n",
                    simpleFocDumpEnabled ? "ON" : "OFF");
#else
      Serial.println("ERROR command=D reason=SIMPLEFOC_DISABLED");
#endif
    } else if (command == '\r' || command == '\n' || command == ' ' ||
               command == '\t') {
      continue;
    } else {
      Serial.printf("ERROR command=%c reason=UNKNOWN_COMMAND\n", command);
    }
  }
}

#if HALL_USE_SIMPLEFOC
void printSimpleFocDump() {
  if (!simpleFocDumpEnabled) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastSimpleFocDumpMs < kSimpleFocDumpIntervalMs) {
    return;
  }
  lastSimpleFocDumpMs = nowMs;

  hallSensor.update();
  int32_t step = 0;
  int32_t interruptCount = 0;
  readSimpleFocCounters(step, interruptCount);
  const int32_t captureStartStep =
      (capture.active || capture.hasResult) ? capture.simpleFocStartStep : step;

  Serial.printf(
      "DUMP t_ms=%lu abc=", static_cast<unsigned long>(nowMs));
  printHallState(readHallStateFast());
  Serial.printf(
      " sf_step=%ld sf_angle_rad=%.6f sf_capture_delta_rad=%.6f "
      "sf_velocity_rad_s=%.6f interrupts=%ld\n",
      static_cast<long>(step), hallSensor.getAngle(),
      simpleFocAngleForStep(step - captureStartStep),
      hallSensor.getVelocity(), static_cast<long>(interruptCount));
}
#endif

void printHeartbeat() {
  const uint32_t nowMs = millis();
  if (nowMs - lastHeartbeatMs < kHeartbeatIntervalMs) {
    return;
  }
  lastHeartbeatMs = nowMs;

  Serial.printf("ALIVE t_ms=%lu abc=", static_cast<unsigned long>(nowMs));
  printHallState(readHallStateFast());
  Serial.printf(" live_edges=%lu capture=%s test_edges=%u dropped=%lu\n",
                static_cast<unsigned long>(liveEdges),
                capture.active ? "RUNNING" : "IDLE",
                capture.validTransitions,
                static_cast<unsigned long>(droppedEvents));
}

}  // namespace

void setup() {
  // Establish low output latches before changing the phase-control pins to
  // outputs. No driver or PWM object is ever initialized by this diagnostic.
  constexpr uint8_t pwmPins[] = {kMotor0PwmA, kMotor0PwmB, kMotor0PwmC,
                                 kMotor1PwmA, kMotor1PwmB, kMotor1PwmC};
  for (const uint8_t pin : pwmPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }

  // Deliberately do not enable the ESP32's internal pull-ups. The external
  // 3.3 V pull-up board is part of the hardware path under test.
#if HALL_USE_SIMPLEFOC
  hallSensor.pullup = Pullup::USE_EXTERN;
  hallSensor.init();
#else
  pinMode(kHallPinA, INPUT);
  pinMode(kHallPinB, INPUT);
  pinMode(kHallPinC, INPUT);
#endif

  Serial.begin(HALL_SERIAL_BAUD);
  delay(100);

  currentState = readHallStateFast();
#if HALL_USE_SIMPLEFOC
  hallSensor.enableInterrupts(handleHallEdgeA, handleHallEdgeB,
                              handleHallEdgeC);
#else
  attachInterrupt(digitalPinToInterrupt(kHallPinA), handleHallEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(kHallPinB), handleHallEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(kHallPinC), handleHallEdge, CHANGE);
#endif

  Serial.printf(
      "READY app=%s baud=%lu A=%u B=%u C=%u "
      "motor0_pwm=LOW motor1_pwm=LOW pwm_state=OFF abc=",
#if HALL_USE_SIMPLEFOC
      "simplefoc-hall-validation",
#else
      "hall-validation",
#endif
      static_cast<unsigned long>(HALL_SERIAL_BAUD), kHallPinA, kHallPinB,
      kHallPinC);
  printHallState(currentState);
#if HALL_USE_SIMPLEFOC
  Serial.printf(" simplefoc=2.4.0 pole_pairs=%u pullups=EXTERNAL",
                kMotorPolePairs);
#endif
  Serial.println();
#if HALL_USE_SIMPLEFOC
  Serial.println(
      "COMMANDS R=start_one_revolution S=summary ?=status "
      "D=toggle_20hz_dump");
#else
  Serial.println("COMMANDS R=start_one_revolution S=summary ?=status");
#endif
}

void loop() {
#if HALL_USE_SIMPLEFOC
  hallSensor.update();
#endif
  HallEvent event{};
  while (popEvent(event)) {
    printLiveEvent(event);
    updateCapture(event);
  }

  processSerialInput();
  printHeartbeat();
#if HALL_USE_SIMPLEFOC
  printSimpleFocDump();
#endif
}
