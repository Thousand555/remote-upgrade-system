"""Run destructive M7 upgrade cycles through the ESP32 gateway console."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import time
from pathlib import Path
from typing import Optional

try:
    import serial  # type: ignore
except ImportError:  # Parser unit tests do not require pyserial.
    serial = None


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOG = REPOSITORY_ROOT / "build" / "m7_stability_result.txt"
PROMPT = "gateway>"
SUCCESS_MARKER = "M7 local UART upgrade completed successfully"
FAILURE_MARKER = "M7 upgrade failed:"
ANSI_ESCAPE = re.compile(r"\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")
CURSOR_POSITION_QUERY = b"\x1b[6n"
CURSOR_POSITION_FALLBACK = b"\x1b[R"


def clean_console_text(text: str) -> str:
    return ANSI_ESCAPE.sub("", text).replace("\r", "").replace("\x00", "")


def terminal_query_response(previous_tail: bytes, data: bytes) -> tuple[bytes, bytes]:
    """Return replies for ESP-IDF linenoise cursor queries, including split input."""
    combined = previous_tail + data
    query_count = combined.count(CURSOR_POSITION_QUERY)
    tail_length = len(CURSOR_POSITION_QUERY) - 1
    tail = combined[-tail_length:] if tail_length else b""
    return tail, CURSOR_POSITION_FALLBACK * query_count


def require_firmware_validation(output: str, expected_version: int) -> int:
    text = clean_console_text(output)
    if "Firmware package invalid:" in text or "No valid package" in text:
        raise RuntimeError("ESP32 rejected the local STM32 package")
    version_match = re.search(r"Firmware version:\s*(\d+)", text)
    size_match = re.search(r"Image size:\s*(\d+)\s+bytes", text)
    identity_match = re.search(
        r"Product/Hardware:\s*0x([0-9A-Fa-f]{4})/0x([0-9A-Fa-f]{4})",
        text,
    )
    if version_match is None or size_match is None or identity_match is None:
        raise RuntimeError("firmware validate output is incomplete")
    if int(version_match.group(1)) != expected_version:
        raise RuntimeError(
            f"package version {version_match.group(1)} does not match "
            f"expected version {expected_version}"
        )
    if (int(identity_match.group(1), 16), int(identity_match.group(2), 16)) != (
        1,
        1,
    ):
        raise RuntimeError("package Product/Hardware is not 0x0001/0x0001")
    return int(size_match.group(1))


def require_success_status(output: str, image_size: int, expected_version: int) -> str:
    text = clean_console_text(output)
    state_match = re.search(r"State:\s*(\w+)", text)
    session_match = re.search(r"Session:\s*(0x[0-9A-Fa-f]{8})", text)
    version_match = re.search(r"Firmware version:\s*(\d+)", text)
    progress_match = re.search(r"Progress:\s*(\d+)/(\d+)\s+bytes", text)
    result_match = re.search(
        r"Last result:\s*ESP_OK,\s*device status=(\d+)", text
    )
    if None in (
        state_match,
        session_match,
        version_match,
        progress_match,
        result_match,
    ):
        raise RuntimeError("upgrade status output is incomplete")
    assert state_match is not None
    assert session_match is not None
    assert version_match is not None
    assert progress_match is not None
    assert result_match is not None
    if state_match.group(1) != "SUCCESS":
        raise RuntimeError(f"upgrade ended in state {state_match.group(1)}")
    if int(version_match.group(1)) != expected_version:
        raise RuntimeError("upgrade status reports the wrong firmware version")
    transferred = int(progress_match.group(1))
    total = int(progress_match.group(2))
    if transferred != image_size or total != image_size:
        raise RuntimeError(
            f"upgrade progress {transferred}/{total} does not match {image_size}"
        )
    if int(result_match.group(1)) != 0:
        raise RuntimeError("device status is not OK")
    return session_match.group(1).upper()


def require_application_probe(output: str, expected_version: int) -> None:
    text = clean_console_text(output)
    service_match = re.search(r"Service:\s*capabilities=0x([0-9A-Fa-f]{4})", text)
    identity_match = re.search(
        r"Device:\s*product=0x([0-9A-Fa-f]{4}),\s*hardware=0x([0-9A-Fa-f]{4})",
        text,
    )
    version_match = re.search(r"application=(\d+)", text)
    app_match = re.search(
        r"APP:\s*base=0x([0-9A-Fa-f]{8}),\s*max_size=0x([0-9A-Fa-f]{8})",
        text,
    )
    if None in (service_match, identity_match, version_match, app_match):
        raise RuntimeError("upgrade probe output is incomplete")
    assert service_match is not None
    assert identity_match is not None
    assert version_match is not None
    assert app_match is not None
    if int(service_match.group(1), 16) & 0x0002 == 0:
        raise RuntimeError("probe did not discover the STM32 APP service")
    if (int(identity_match.group(1), 16), int(identity_match.group(2), 16)) != (
        1,
        1,
    ):
        raise RuntimeError("STM32 Product/Hardware mismatch")
    if int(version_match.group(1)) != expected_version:
        raise RuntimeError("STM32 APP version mismatch")
    if (int(app_match.group(1), 16), int(app_match.group(2), 16)) != (
        0x08020000,
        0x000E0000,
    ):
        raise RuntimeError("STM32 APP layout mismatch")


class GatewayConsole:
    def __init__(
        self,
        port: str,
        baud: int,
        log_path: Path,
        append: bool = False,
        label: str = "M7 console automation",
    ):
        if serial is None:
            raise RuntimeError("pyserial is required: install it in the active Python environment")
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = log_path.open(
            "a" if append else "w", encoding="utf-8", newline=""
        )
        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=baud,
                timeout=0.1,
                write_timeout=1.0,
            )
        except Exception:
            self._log.close()
            raise
        self._terminal_query_tail = b""
        self._label = label
        self.record(f"{label} started: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        self.record(f"Port={port}, baud={baud}\n")

    def close(self) -> None:
        self.record(
            f"{self._label} stopped: {time.strftime('%Y-%m-%d %H:%M:%S')}\n"
        )
        self._serial.close()
        self._log.close()

    def record(self, text: str) -> None:
        self._log.write(text)
        self._log.flush()
        sys.stdout.write(text)
        sys.stdout.flush()

    def _write_command(self, command: str) -> None:
        self.record(f"\n>>> {command}\n")
        self._serial.write((command + "\r\n").encode("utf-8"))
        self._serial.flush()

    def _read_available(self) -> str:
        waiting = self._serial.in_waiting
        data = self._serial.read(waiting if waiting > 0 else 1)
        if not data:
            return ""
        self._terminal_query_tail, response = terminal_query_response(
            self._terminal_query_tail, data
        )
        if response:
            # ESP-IDF linenoise asks for the cursor position before accepting a
            # command.  A non-interactive serial client has no real cursor, so
            # end the query with an intentionally unparseable response.  The
            # console then uses its documented 80-column fallback without
            # consuming the first bytes of the next command.
            self._serial.write(response)
            self._serial.flush()
        text = data.decode("utf-8", errors="replace")
        self.record(text)
        return text

    def drain(self, quiet_seconds: float = 0.2, maximum_seconds: float = 2.0) -> str:
        output = ""
        deadline = time.monotonic() + maximum_seconds
        quiet_deadline = time.monotonic() + quiet_seconds
        while time.monotonic() < deadline:
            text = self._read_available()
            if text:
                output += text
                quiet_deadline = time.monotonic() + quiet_seconds
            elif time.monotonic() >= quiet_deadline:
                break
        return output

    def read_until_any(self, markers: tuple[str, ...], timeout: float) -> str:
        output = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output += self._read_available()
            clean = clean_console_text(output)
            if any(marker in clean for marker in markers):
                return output
        raise TimeoutError(f"console timeout waiting for: {', '.join(markers)}")

    def read_until_prompt(self, submitted_command: str, timeout: float) -> str:
        """Wait for a prompt occurring after the submitted command line."""
        output = ""
        deadline = time.monotonic() + timeout
        submission_end: Optional[int] = None
        while time.monotonic() < deadline:
            output += self._read_available()
            clean = clean_console_text(output)
            if submission_end is None:
                if submitted_command:
                    submitted = re.search(
                        re.escape(submitted_command) + r"[^\n]*\n", clean
                    )
                    if submitted is not None:
                        submission_end = submitted.end()
                else:
                    line_end = clean.find("\n")
                    if line_end >= 0:
                        submission_end = line_end + 1
            if submission_end is not None and PROMPT in clean[submission_end:]:
                return output
        raise TimeoutError(
            f"console timeout waiting for completed command: {submitted_command!r}"
        )

    def read_until_transfer_progress(
        self, minimum_bytes: int, timeout: float
    ) -> tuple[str, int, int]:
        """Wait until a TRANSFER progress log reaches ``minimum_bytes``."""
        output = ""
        deadline = time.monotonic() + timeout
        pattern = re.compile(r"Transferred\s+(\d+)/(\d+)\s+bytes")
        while time.monotonic() < deadline:
            output += self._read_available()
            matches = pattern.findall(clean_console_text(output))
            if matches:
                transferred, total = (int(value) for value in matches[-1])
                if transferred >= minimum_bytes:
                    return output, transferred, total
            clean = clean_console_text(output)
            if FAILURE_MARKER in clean:
                raise RuntimeError(
                    "upgrade failed before reaching the requested trigger progress"
                )
            if SUCCESS_MARKER in clean:
                raise RuntimeError(
                    "upgrade completed before reaching the requested trigger progress"
                )
        raise TimeoutError(
            f"console timeout waiting for transfer progress >= {minimum_bytes}"
        )

    def synchronize(self) -> None:
        # A previous non-interactive client may have closed the port while
        # linenoise was blocked in getCursorPosition().  Complete that stale
        # query and submit an empty line before looking for a fresh prompt.
        # In dumb mode this can only produce a harmless rejected empty/bootstrap
        # line, which is drained below.
        self._serial.write(CURSOR_POSITION_FALLBACK + b"\r\n")
        self._serial.flush()
        self.drain()
        self._write_command("")
        self.read_until_prompt("", 5.0)

    def command(self, command: str, timeout: float = 10.0) -> str:
        self.drain()
        self._write_command(command)
        return self.read_until_prompt(command, timeout)

    def wait_for_upgrade(self, initial_output: str, timeout: float) -> str:
        clean = clean_console_text(initial_output)
        if SUCCESS_MARKER in clean or FAILURE_MARKER in clean:
            return initial_output
        return initial_output + self.read_until_any(
            (SUCCESS_MARKER, FAILURE_MARKER), timeout
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run repeated destructive upgrades through the ESP32 console"
    )
    parser.add_argument("--port", required=True, help="ESP32 console COM port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--version", type=int, default=1)
    parser.add_argument("--cycle-timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--clear-faults", action="store_true")
    parser.add_argument("--confirm-destructive", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error("--cycles must be in the range 1..100")
    if not 1 <= args.version <= 0xFFFFFFFF:
        parser.error("--version must be in the range 1..0xFFFFFFFF")
    if args.cycle_timeout <= 0:
        parser.error("--cycle-timeout must be positive")
    if not args.confirm_destructive:
        parser.error("this test erases STM32 APP repeatedly; add --confirm-destructive")
    return args


def run(args: argparse.Namespace) -> None:
    console = GatewayConsole(
        args.port,
        args.baud,
        args.log.resolve(),
        label="M7 stability test",
    )
    passed = 0
    started = time.monotonic()
    try:
        console.synchronize()
        if args.clear_faults:
            clear_output = console.command("test fault clear")
            if "All reliability faults cleared." not in clean_console_text(clear_output):
                raise RuntimeError("test firmware or fault-clear command is unavailable")

        for cycle in range(1, args.cycles + 1):
            console.record(f"\n========== M7 cycle {cycle}/{args.cycles} ==========\n")
            cycle_started = time.monotonic()

            validate_output = console.command("firmware validate")
            image_size = require_firmware_validation(validate_output, args.version)
            pre_probe = console.command("upgrade probe")
            require_application_probe(pre_probe, args.version)

            start_output = console.command("upgrade start")
            if "Command accepted." not in clean_console_text(start_output):
                raise RuntimeError("gateway rejected upgrade start")
            outcome = console.wait_for_upgrade(start_output, args.cycle_timeout)
            if FAILURE_MARKER in clean_console_text(outcome):
                raise RuntimeError("gateway reported an upgrade failure")

            status_output = console.command("upgrade status")
            session = require_success_status(status_output, image_size, args.version)
            post_probe = console.command("upgrade probe")
            require_application_probe(post_probe, args.version)

            passed += 1
            elapsed = time.monotonic() - cycle_started
            console.record(
                f"\n[PASS] cycle {cycle}/{args.cycles}: "
                f"session={session}, elapsed={elapsed:.3f}s\n"
            )

        total = time.monotonic() - started
        console.record(
            f"\n[PASS] M7 stability test: {passed}/{args.cycles} cycles, "
            f"elapsed={total:.3f}s\n"
        )
    finally:
        console.close()


def main() -> None:
    args = parse_args()
    log_path = args.log.resolve()
    try:
        run(args)
    except Exception as exc:
        print(f"\n[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    digest = hashlib.sha256(log_path.read_bytes()).hexdigest().upper()
    print(f"Log: {log_path}")
    print(f"Log SHA-256: {digest}")


if __name__ == "__main__":
    main()
