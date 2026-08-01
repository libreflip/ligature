#include <stdio.h>
#include <string.h>

#include "motor_smoke/alignment_failure.h"
#include "motor_smoke/hall_direction.h"

namespace {

bool expectReason(const AlignmentEvidence& evidence, const char* expected) {
  const char* actual = classifyAlignmentFailure(evidence);
  const bool matches = expected == nullptr ? actual == nullptr
                                           : actual != nullptr &&
                                                 strcmp(actual, expected) == 0;
  if (!matches) {
    fprintf(stderr, "expected %s, got %s\n",
            expected == nullptr ? "PASS" : expected,
            actual == nullptr ? "PASS" : actual);
  }
  return matches;
}

}  // namespace

int main() {
  const HallDirectionSummary reverseOnly = summarizeHallDirection(0, 24);
  if (reverseOnly.direction != -1 || reverseOnly.minorityTransitions != 0U ||
      reverseOnly.netTransitions != 24U) {
    fprintf(stderr, "reverse-only Hall summary failed\n");
    return 1;
  }

  const HallDirectionSummary startupBackstep =
      summarizeHallDirection(23, 1);
  if (startupBackstep.direction != 1 ||
      startupBackstep.minorityTransitions != 1U ||
      startupBackstep.netTransitions != 22U) {
    fprintf(stderr, "startup-backstep Hall summary failed\n");
    return 1;
  }

  const HallDirectionSummary fullNetRevolution =
      summarizeHallDirection(25, 1);
  if (fullNetRevolution.direction != 1 ||
      fullNetRevolution.minorityTransitions != 1U ||
      fullNetRevolution.netTransitions != 24U) {
    fprintf(stderr, "full-net-revolution Hall summary failed\n");
    return 1;
  }

  AlignmentEvidence evidence = {
      .focResult = 0,
      .hallEdges = 0,
      .hallStartLegal = true,
      .hallLegal = true,
      .currentSamples = 0,
      .currentFinite = true,
      .peakCurrent = 0.0F,
      .currentCutoff = 1.0F,
  };

  if (!expectReason(evidence, "SENSOR_ALIGNMENT_NO_HALL_MOTION")) {
    return 1;
  }

  evidence.hallEdges = 3;
  if (!expectReason(evidence, "SENSOR_ALIGNMENT_FAILED")) {
    return 1;
  }

  evidence.currentSamples = 42;
  if (!expectReason(evidence, "CURRENT_SENSE_ALIGNMENT_FAILED")) {
    return 1;
  }

  evidence.focResult = 1;
  evidence.peakCurrent = 1.0F;
  if (!expectReason(evidence, "ALIGNMENT_CURRENT_IMPLAUSIBLE")) {
    return 1;
  }

  evidence.peakCurrent = 0.2F;
  if (!expectReason(evidence, nullptr)) {
    return 1;
  }

  evidence.hallStartLegal = false;
  if (!expectReason(evidence, "ALIGNMENT_HALL_START_INVALID")) {
    return 1;
  }

  puts("GREEN: alignment failures preserve the first failing stage");
  return 0;
}
