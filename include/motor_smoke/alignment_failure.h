#pragma once

#include <stdint.h>

struct AlignmentEvidence {
  int focResult;
  uint32_t hallEdges;
  bool hallStartLegal;
  bool hallLegal;
  uint32_t currentSamples;
  bool currentFinite;
  float peakCurrent;
  float currentCutoff;
};

inline const char* classifyAlignmentFailure(
    const AlignmentEvidence& evidence) {
  if (!evidence.hallStartLegal) {
    return "ALIGNMENT_HALL_START_INVALID";
  }
  if (!evidence.hallLegal) {
    return "ALIGNMENT_HALL_INVALID";
  }
  if (!evidence.focResult) {
    if (evidence.currentSamples == 0U) {
      return evidence.hallEdges == 0U
                 ? "SENSOR_ALIGNMENT_NO_HALL_MOTION"
                 : "SENSOR_ALIGNMENT_FAILED";
    }
    return "CURRENT_SENSE_ALIGNMENT_FAILED";
  }
  if (!evidence.currentFinite || evidence.currentSamples == 0U ||
      evidence.peakCurrent >= evidence.currentCutoff) {
    return "ALIGNMENT_CURRENT_IMPLAUSIBLE";
  }

  return nullptr;
}
