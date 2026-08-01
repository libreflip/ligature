#pragma once

#include <stdint.h>

struct HallDirectionSummary {
  int8_t direction;
  uint32_t minorityTransitions;
  uint32_t netTransitions;
};

inline HallDirectionSummary summarizeHallDirection(
    const uint32_t forwardTransitions,
    const uint32_t reverseTransitions) {
  if (forwardTransitions > reverseTransitions) {
    return {1, reverseTransitions, forwardTransitions - reverseTransitions};
  }
  if (reverseTransitions > forwardTransitions) {
    return {-1, forwardTransitions, reverseTransitions - forwardTransitions};
  }
  return {0, forwardTransitions, 0};
}
