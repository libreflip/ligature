#!/usr/bin/env python3
"""Run the fixed Motor 1 smoke test and retain its serial transcript."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import serial
from serial.tools import list_ports


BAUD_RATE = 500000
SERIAL_READ_TIMEOUT_S = 0.20
READY_TIMEOUT_S = 15.0
ALIGNMENT_TIMEOUT_S = 25.0
TEST_TIMEOUT_S = 60.0
REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_DIR = REPO_ROOT / ".scratch" / "esp32-lift-axis-firmware" / "evidence"


@dataclass
class RunState:
    run_sent: bool = False
    alignment_started_at: float | None = None
    alignment_finished: bool = False
    terminal_line: str | None = None
    saw_fail: bool = False


class Transcript:
    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self._file = path.open("x", encoding="utf-8", buffering=1)
        self._started_at = time.monotonic()

    def write(self, source: str, message: str) -> None:
        elapsed = time.monotonic() - self._started_at
        record = f"[{elapsed:08.3f}] {source} {message}"
        print(record, flush=True)
        self._file.write(record + "\n")

    def close(self) -> None:
        self._file.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the fixed, supervised Motor 1 smoke test."
    )
    parser.add_argument(
        "--port",
        help="serial device; required only when auto-detection is ambiguous",
    )
    return parser.parse_args()


def is_serial_candidate(port: list_ports.ListPortInfo) -> bool:
    description = " ".join(
        value for value in (port.device, port.description, port.manufacturer) if value
    ).lower()
    if "bluetooth" in description:
        return False
    if port.vid is not None:
        return True
    return any(
        marker in description
        for marker in ("usb", "uart", "serial", "ttyacm", "ttyusb", "wch")
    )


def select_port(requested_port: str | None) -> str:
    if requested_port:
        return requested_port

    candidates = [port for port in list_ports.comports() if is_serial_candidate(port)]
    if len(candidates) == 1:
        return candidates[0].device

    if not candidates:
        raise RuntimeError(
            "no USB/UART serial candidate found; connect the board or pass --port"
        )

    details = "\n".join(
        f"  {port.device}: {port.description}" for port in candidates
    )
    raise RuntimeError(
        "multiple serial candidates found; rerun with --port <device>:\n" + details
    )


def new_transcript_path() -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = EVIDENCE_DIR / f"motor-smoke-{timestamp}.txt"
    suffix = 1
    while candidate.exists():
        candidate = EVIDENCE_DIR / f"motor-smoke-{timestamp}-{suffix}.txt"
        suffix += 1
    return candidate


def confirm_operator(port: str) -> None:
    print(
        "\nThis test will align and move the standalone Motor 1. Confirm all of "
        "the following:\n"
        "- the unloaded motor is secured and its shaft is clear;\n"
        "- motor U/V/W were connected to board A1/B1/C1 only while unplugged;\n"
        "- the Mean Well mains plug is immediately reachable;\n"
        "- you will pull mains for sustained motion, violent vibration, smoke, "
        "odor, or rapid heating.\n"
        f"Serial device: {port}\n"
    )
    response = input("Type RUN to perform the one fixed smoke sequence: ").strip()
    if response != "RUN":
        raise RuntimeError("operator confirmation was not RUN; nothing was sent")


def read_line(device: serial.Serial) -> str | None:
    raw = device.readline()
    if not raw:
        return None
    return raw.decode("utf-8", errors="replace").strip()


def wait_for_ready(device: serial.Serial, transcript: Transcript) -> None:
    deadline = time.monotonic() + READY_TIMEOUT_S
    while time.monotonic() < deadline:
        line = read_line(device)
        if not line:
            continue
        transcript.write("ESP", line)
        if line.startswith("READY app=motor-smoke "):
            if "pwm_state=OFF" not in line:
                raise RuntimeError("firmware READY record did not report zero PWM")
            return
    raise RuntimeError("timed out waiting for motor-smoke READY with zero PWM")


def update_state(line: str, state: RunState) -> None:
    if state.run_sent and (line.startswith("BOOT ") or line.startswith("READY ")):
        raise RuntimeError("ESP32 reset after RUN")

    if line.startswith("STAGE alignment status=START"):
        state.alignment_started_at = time.monotonic()
    elif line.startswith("STAGE alignment status=PASS") or line.startswith(
        "STAGE alignment status=FAIL"
    ):
        state.alignment_finished = True

    if "status=FAIL" in line or line.startswith("FAIL"):
        state.saw_fail = True
    if line.startswith("RESULT "):
        state.terminal_line = line


def pull_mains_message(reason: str) -> str:
    return (
        f"FAIL reason={reason} action=PULL_MAINS_NOW; closing the serial port "
        "cannot force PWM off while firmware is blocked or unreachable, and "
        "this board has no driver-enable pins"
    )


def wait_for_result(device: serial.Serial, transcript: Transcript) -> RunState:
    state = RunState(run_sent=True)
    started_at = time.monotonic()
    device.write(b"RUN\n")
    device.flush()
    transcript.write("HOST", "sent=RUN")

    while state.terminal_line is None:
        now = time.monotonic()
        if (
            state.alignment_started_at is not None
            and not state.alignment_finished
            and now - state.alignment_started_at > ALIGNMENT_TIMEOUT_S
        ):
            raise TimeoutError(pull_mains_message("ALIGNMENT_TIMEOUT"))
        if now - started_at > TEST_TIMEOUT_S:
            raise TimeoutError(pull_mains_message("TEST_TIMEOUT"))

        line = read_line(device)
        if not line:
            continue
        transcript.write("ESP", line)
        update_state(line, state)

    return state


def validate_result(state: RunState) -> None:
    assert state.terminal_line is not None
    terminal = state.terminal_line
    if not all(
        token in terminal
        for token in ("motor0_pwm=LOW", "motor1_pwm=ZERO", "pwm_state=OFF")
    ):
        raise RuntimeError("terminal result did not report all PWM commands off")
    if state.saw_fail or "status=PASS" not in terminal:
        raise RuntimeError("firmware reported FAIL")


def main() -> int:
    args = parse_args()
    try:
        port = select_port(args.port)
        confirm_operator(port)
    except (EOFError, KeyboardInterrupt, RuntimeError) as error:
        print(f"motor smoke test not started: {error}", file=sys.stderr)
        return 2

    transcript = Transcript(new_transcript_path())
    transcript.write("HOST", f"port={port} baud={BAUD_RATE}")
    transcript.write("HOST", "operator_confirmation=RUN")
    active_run = False
    try:
        with serial.Serial(
            port=port,
            baudrate=BAUD_RATE,
            timeout=SERIAL_READ_TIMEOUT_S,
            write_timeout=2.0,
        ) as device:
            device.reset_input_buffer()
            wait_for_ready(device, transcript)
            active_run = True
            state = wait_for_result(device, transcript)
            if state.terminal_line is not None and all(
                token in state.terminal_line
                for token in (
                    "motor0_pwm=LOW",
                    "motor1_pwm=ZERO",
                    "pwm_state=OFF",
                )
            ):
                active_run = False
            validate_result(state)
    except TimeoutError as error:
        transcript.write("HOST", str(error))
        return 3
    except (OSError, serial.SerialException) as error:
        message = f"FAIL reason=SERIAL_LOST detail={error}"
        if active_run:
            message = pull_mains_message("SERIAL_LOST") + f" detail={error}"
        transcript.write("HOST", message)
        return 4
    except (RuntimeError, AssertionError) as error:
        message = f"FAIL reason={error}"
        if active_run:
            message += " action=PULL_MAINS_NOW"
        transcript.write("HOST", message)
        return 5
    except KeyboardInterrupt:
        message = "FAIL reason=INTERRUPTED"
        if active_run:
            message += " action=PULL_MAINS_NOW"
        transcript.write("HOST", message)
        return 130
    finally:
        transcript.write("HOST", f"transcript={transcript.path}")
        transcript.close()

    print(f"\nPASS — transcript saved to {transcript.path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
