#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "production/controller.h"

namespace {

class RecordingSink : public ligature::LineSink {
 public:
  void writeLine(const char* line) override { lines.emplace_back(line); }

  bool lastContains(const char* text) const {
    return !lines.empty() && lines.back().find(text) != std::string::npos;
  }

  std::vector<std::string> lines;
};

ligature::CommissionedConfig validConfig() {
  return {
      true, 1.0F, -100.0F, -2.0F, 0.2F, 20.0F, 5.0F, 10.0F, 3.0F,
      1.5F, 2.9F, 3.0F, 24.0F, 50.0F, 60.0F, 0.1F, 0.05F, 0.2F,
      0.1F, 0.1F, 0.05F, 1.5F, 50.0F,
  };
}

ligature::Observation observation(uint32_t now, float angle = 0.0F,
                                  float velocity = 0.0F,
                                  float current = 0.0F,
                                  bool endstop = false) {
  return {now, angle, velocity, current, endstop, true, true, false, false};
}

void send(ligature::Controller& controller, const char* line,
          const ligature::Observation& value) {
  controller.acceptLine(line, strlen(line), value);
}

bool expect(bool condition, const char* message) {
  if (!condition) fprintf(stderr, "FAIL: %s\n", message);
  return condition;
}

}  // namespace

int main() {
  RecordingSink output;
  ligature::CommissionedConfig config = validConfig();
  ligature::Controller controller(config, output);
  controller.reset(observation(0), true);

  send(controller, "M30", observation(1));
  if (!expect(output.lastContains("UNKNOWN_COMMAND"),
              "M3 prefix cannot arm") ||
      !expect(controller.state() == ligature::State::IdleUntrusted,
              "malformed arm leaves state unchanged")) return 1;

  send(controller, "m3", observation(2));
  send(controller, "M3 X1", observation(3));
  if (!expect(output.lines[output.lines.size() - 2].find("SYNTAX") !=
                  std::string::npos,
              "lowercase rejected") ||
      !expect(output.lastContains("INVALID_PARAM"),
              "arm parameters rejected")) return 1;

  send(controller, "M3", observation(4));
  send(controller, "G1 Z-10", observation(5));
  if (!expect(controller.state() == ligature::State::ArmedUntrusted,
              "arm starts untrusted") ||
      !expect(output.lastContains("POSITION_UNTRUSTED"),
              "absolute move needs trust")) return 1;

  send(controller, "M52", observation(6));
  send(controller, "G1 D-5 F300", observation(7));
  if (!expect(controller.state() == ligature::State::MovingUntrusted,
              "override consumed by relative move") ||
      !expect(output.lastContains("ok G1"), "exclusive move acknowledged"))
    return 1;
  controller.tick(observation(20, -5.0F));
  controller.tick(observation(80, -5.0F));
  if (!expect(controller.state() == ligature::State::ArmedUntrusted,
              "untrusted move returns armed-untrusted") ||
      !expect(output.lastContains("done G1"), "move has one terminal"))
    return 1;

  send(controller, "G28", observation(100, -5.0F));
  controller.tick(observation(110, -4.0F, 0.0F, 0.0F, true));
  controller.tick(observation(120, -3.5F, 0.0F, 0.0F, false));
  controller.tick(observation(130, -5.5F));
  controller.tick(observation(200, -5.5F));
  if (!expect(controller.state() == ligature::State::Ready,
              "homing release defines zero and pulls off") ||
      !expect(output.lastContains("done G28 Z:-2.000"),
              "homing reports negative top limit")) return 1;

  send(controller, "G0 Z0", observation(210, -5.5F));
  if (!expect(output.lastContains("SOFT_LIMIT"),
              "target above top limit rejected")) return 1;

  send(controller, "G1 Z-20 F300", observation(220, -5.5F));
  send(controller, "M53", observation(230, -8.0F));
  if (!expect(controller.state() == ligature::State::Ready,
              "routine stop preserves trust and arming") ||
      !expect(output.lastContains("CANCELLED:G1"),
              "routine stop retires active command")) return 1;

  send(controller, "M112", observation(240, -8.0F));
  if (!expect(controller.state() == ligature::State::FaultTrusted,
              "emergency stop latches trusted fault") ||
      !expect(output.lastContains("CANCELLED:NONE"),
              "emergency stop has one terminal")) return 1;
  send(controller, "G1 Z-10", observation(250, -8.0F));
  if (!expect(output.lastContains("FAULTED"), "fault gate enforced")) return 1;
  send(controller, "M999", observation(260, -8.0F));
  send(controller, "M3", observation(270, -8.0F));

  send(controller, "M155 S0.2", observation(280, -8.0F));
  controller.tick(observation(500, -8.0F));
  if (!expect(output.lastContains("status STATE:READY TRUST:1"),
              "heartbeat derives stable state and trust")) return 1;

  send(controller, "G30 P40", observation(510, -8.0F));
  controller.tick(observation(650, -9.0F, -5.0F, 0.1F));
  controller.tick(observation(800, -9.0F, 0.0F, 0.6F));
  controller.tick(observation(860, -9.0F, 0.0F, 0.6F));
  if (!expect(controller.state() == ligature::State::Holding,
              "touchdown requires progress then sustained saturation") ||
      !expect(output.lastContains("done G30") ||
                  output.lines[output.lines.size() - 2].find("done G30") !=
                      std::string::npos,
              "touchdown reports terminal before heartbeat")) return 1;
  send(controller, "M24", observation(870, -9.0F));
  if (!expect(controller.state() == ligature::State::Ready,
              "M24 atomically returns to angle hold")) return 1;

  send(controller, "M120 N3 D1", observation(880, -9.0F));
  send(controller, "G1 Z-15", observation(890, -9.0F));
  controller.tick(observation(900, -10.0F, -1.0F, 0.4F));
  send(controller, "M53", observation(910, -10.0F));
  send(controller, "M5", observation(920, -10.0F));
  send(controller, "M121", observation(930, -10.0F));
  if (!expect(output.lastContains("done M121 COUNT:1"),
              "capture buffers during motion and transfers only PWM-off"))
    return 1;
  send(controller, "M122", observation(940, -10.0F));

  RecordingSink invalidOutput;
  ligature::CommissionedConfig invalid = validConfig();
  invalid.commissioned = false;
  ligature::Controller invalidController(invalid, invalidOutput);
  invalidController.reset(observation(0), true);
  send(invalidController, "M3", observation(1));
  if (!expect(invalidController.state() == ligature::State::CommissioningOnly,
              "missing commissioned marker fails closed") ||
      !expect(invalidOutput.lastContains("CONFIG_INVALID"),
              "invalid configuration cannot arm")) return 1;

  puts("GREEN: production protocol/state-machine conformance baseline passes");
  return 0;
}
