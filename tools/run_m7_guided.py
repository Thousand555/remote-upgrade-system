"""Run guided physical and automated M7 reliability tests R05/R06/R09."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import time
from pathlib import Path
from typing import Optional

from run_m7_reliability import (
    parse_bootloader_probe,
    parse_upgrade_status,
    safe_diagnostics,
)
from run_m7_stability import (
    FAILURE_MARKER,
    SUCCESS_MARKER,
    GatewayConsole,
    clean_console_text,
    require_application_probe,
    require_firmware_validation,
    require_success_status,
)


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD = REPOSITORY_ROOT / "build"
CASE_IDS = ("R05A", "R05B", "R06", "R09")
DATA_RETRY_PATTERN = re.compile(
    r"Command 0x12 attempt (\d)/5 (?:failed|decode failed|returned an invalid response)"
)


def require_uart_retry_evidence(output: str) -> list[int]:
    attempts = [int(value) for value in DATA_RETRY_PATTERN.findall(clean_console_text(output))]
    if not attempts:
        raise RuntimeError("no DATA retry was observed during the physical UART interruption")
    return attempts


def physical_break_countdown(console: GatewayConsole, seconds: float) -> float:
    console.record(
        "\n[ACTION] Disconnect only STM32 PA9 -> ESP32 GPIO18, then press ENTER.\n"
    )
    input()
    started = time.monotonic()
    console.record("[ACTION] Return line reported disconnected.\n")
    deadline = started + seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        console.record(f"[WAIT] Keep the return line open: {remaining:.1f}s remaining\n")
        time.sleep(min(1.0, remaining))
    console.record("\a[ACTION] Reconnect STM32 PA9 -> ESP32 GPIO18, then press ENTER.\n")
    input()
    duration = time.monotonic() - started
    console.record(f"[ACTION] Return line reconnected after {duration:.3f}s.\n")
    return duration


def clear_faults(console: GatewayConsole) -> None:
    output = console.command("test fault clear")
    if "All reliability faults cleared." not in clean_console_text(output):
        raise RuntimeError("reliability-test firmware or fault-clear command is unavailable")


def require_app_baseline(
    console: GatewayConsole, expected_version: int
) -> int:
    image_size = require_firmware_validation(
        console.command("firmware validate"), expected_version
    )
    require_application_probe(console.command("upgrade probe"), expected_version)
    return image_size


def start_to_trigger(
    console: GatewayConsole, trigger_bytes: int, timeout: float
) -> tuple[str, int, int]:
    start = console.command("upgrade start")
    if "Command accepted." not in clean_console_text(start):
        raise RuntimeError("gateway rejected upgrade start")
    progress_output, transferred, total = console.read_until_transfer_progress(
        trigger_bytes, timeout
    )
    return start + progress_output, transferred, total


def require_success(
    console: GatewayConsole,
    outcome: str,
    image_size: int,
    version: int,
) -> str:
    clean = clean_console_text(outcome)
    if FAILURE_MARKER in clean or SUCCESS_MARKER not in clean:
        raise RuntimeError("upgrade did not end with the gateway success marker")
    status = console.command("upgrade status")
    session = require_success_status(status, image_size, version)
    require_application_probe(console.command("upgrade probe"), version)
    return session


def run_r05(
    console: GatewayConsole,
    args: argparse.Namespace,
    expect_exhaustion: bool,
) -> dict:
    clear_faults(console)
    image_size = require_app_baseline(console, args.version)
    output, trigger_progress, total = start_to_trigger(
        console, args.trigger_bytes, args.trigger_timeout
    )
    if total != image_size:
        raise RuntimeError("transfer total does not match the validated image size")

    duration = physical_break_countdown(console, args.break_seconds)
    outcome = output + console.wait_for_upgrade("", args.case_timeout)
    attempts = require_uart_retry_evidence(outcome)

    if not expect_exhaustion:
        session = require_success(console, outcome, image_size, args.version)
        return {
            "result": "PASS",
            "sessions": [session],
            "evidence": [
                f"physical return-line interruption lasted {duration:.3f}s",
                f"DATA retry attempts observed: {attempts}",
                "current task recovered without a restart",
                f"completed {image_size}/{image_size} and rediscovered APP",
            ],
        }

    clean = clean_console_text(outcome)
    if FAILURE_MARKER not in clean or SUCCESS_MARKER in clean:
        raise RuntimeError("long UART interruption did not exhaust the current task")
    failed_output = console.command("upgrade status")
    failed = parse_upgrade_status(failed_output)
    if failed.state != "FAILED" or failed.error not in (
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_RESPONSE",
    ):
        raise RuntimeError(
            f"unexpected long-interruption result: {failed.state}/{failed.error}"
        )
    boot = parse_bootloader_probe(console.command("upgrade probe"))
    if boot.boot_state != 4:
        raise RuntimeError(f"expected resumable RECEIVING state 4, got {boot.boot_state}")

    recovery_start = console.command("upgrade start")
    recovery_outcome = console.wait_for_upgrade(recovery_start, args.case_timeout)
    resumed_session = require_success(
        console, recovery_outcome, image_size, args.version
    )
    if resumed_session != failed.session:
        raise RuntimeError(
            f"resume changed session {failed.session} -> {resumed_session}"
        )
    return {
        "result": "PASS",
        "sessions": [failed.session, resumed_session],
        "evidence": [
            f"physical return-line interruption lasted {duration:.3f}s",
            f"DATA retry attempts observed: {attempts}",
            f"current task failed as {failed.error}",
            "Bootloader remained RECEIVING(4)",
            "clean retry resumed the same Session and rediscovered APP",
        ],
    }


def run_r06(console: GatewayConsole, args: argparse.Namespace) -> dict:
    clear_faults(console)
    image_size = require_app_baseline(console, args.version)
    output, trigger_progress, total = start_to_trigger(
        console, args.trigger_bytes, args.trigger_timeout
    )
    if total != image_size:
        raise RuntimeError("transfer total does not match the validated image size")

    abort_output = console.command("upgrade abort")
    if "Command accepted." not in clean_console_text(abort_output):
        raise RuntimeError("gateway rejected upgrade abort")
    outcome = output + abort_output + console.wait_for_upgrade("", 15.0)
    if FAILURE_MARKER not in clean_console_text(outcome):
        raise RuntimeError("aborted task did not enter FAILED")

    failed = parse_upgrade_status(console.command("upgrade status"))
    if failed.state != "FAILED" or failed.error != "ESP_ERR_INVALID_STATE":
        raise RuntimeError(f"unexpected abort result: {failed.state}/{failed.error}")
    boot = parse_bootloader_probe(console.command("upgrade probe"))
    if boot.boot_state != 8:
        raise RuntimeError(f"expected Bootloader FAILED state 8, got {boot.boot_state}")

    recovery_start = console.command("upgrade start")
    recovery_outcome = console.wait_for_upgrade(recovery_start, args.case_timeout)
    recovered_session = require_success(
        console, recovery_outcome, image_size, args.version
    )
    if recovered_session == failed.session:
        raise RuntimeError("ABORT recovery reused the failed Session instead of creating a new one")
    return {
        "result": "PASS",
        "sessions": [failed.session, recovered_session],
        "evidence": [
            f"ABORT issued after progress reached {trigger_progress} bytes",
            "aborted task entered FAILED and STM32 reported FAILED(8)",
            "recovery created a new Session, erased, verified, and rediscovered APP",
        ],
    }


def open_console_with_retry(
    port: str,
    baud: int,
    log_path: Path,
    append: bool,
    timeout: float,
) -> GatewayConsole:
    deadline = time.monotonic() + timeout
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        try:
            return GatewayConsole(
                port,
                baud,
                log_path,
                append=append,
                label="M7 guided reliability test",
            )
        except Exception as exc:
            last_error = exc
            time.sleep(1.0)
    raise TimeoutError(f"console did not reconnect within {timeout}s: {last_error}")


def run_r09(args: argparse.Namespace, log_path: Path) -> dict:
    evidence: list[str] = []
    for cycle in range(1, args.cycles + 1):
        print(f"\n========== R09 cold start {cycle}/{args.cycles} ==========")
        input("Power OFF both ESP32 and STM32, then press ENTER: ")
        for remaining in range(args.power_off_seconds, 0, -1):
            print(f"Keep both boards OFF: {remaining}s", flush=True)
            time.sleep(1.0)
        input("Power ON both boards, wait for USB enumeration, then press ENTER: ")

        console = open_console_with_retry(
            args.port,
            args.baud,
            log_path,
            append=cycle > 1,
            timeout=args.reconnect_timeout,
        )
        try:
            console.synchronize()
            image_size = require_firmware_validation(
                console.command("firmware validate"), args.version
            )
            require_application_probe(console.command("upgrade probe"), args.version)
            evidence.append(
                f"cycle {cycle}/{args.cycles}: package {image_size} bytes valid; APP probe OK"
            )
            console.record(f"\n[PASS] R09 cold start {cycle}/{args.cycles}\n")
        finally:
            console.close()
    return {"result": "PASS", "sessions": [], "evidence": evidence}


def write_report(
    path: Path,
    case_id: str,
    result: dict,
    args: argparse.Namespace,
    log_sha256: str,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    sessions = result.get("sessions", [])
    lines = [
        f"# M7 {case_id} guided reliability result",
        "",
        f"- Result: {result['result']}",
        f"- Port: `{args.port}` @ {args.baud}",
        f"- Expected APP version: `{args.version}`",
        f"- Raw log SHA-256: `{log_sha256}`",
    ]
    if sessions:
        lines.append("- Sessions: " + ", ".join(f"`{item}`" for item in sessions))
    lines.extend(("", "## Evidence", ""))
    lines.extend(f"- {item}" for item in result.get("evidence", []))
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run guided M7 physical tests R05A/R05B/R06/R09"
    )
    parser.add_argument("--port", required=True, help="ESP32 console COM port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--case", choices=CASE_IDS, required=True)
    parser.add_argument("--version", type=int, default=2)
    parser.add_argument("--trigger-bytes", type=int, default=65536)
    parser.add_argument("--trigger-timeout", type=float, default=60.0)
    parser.add_argument("--case-timeout", type=float, default=240.0)
    parser.add_argument("--break-seconds", type=float)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--power-off-seconds", type=int, default=10)
    parser.add_argument("--reconnect-timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--confirm-destructive", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.version <= 0xFFFFFFFF:
        parser.error("--version must be in the range 1..0xFFFFFFFF")
    if args.trigger_bytes <= 0:
        parser.error("--trigger-bytes must be positive")
    if args.trigger_timeout <= 0 or args.case_timeout <= 0:
        parser.error("timeouts must be positive")
    if not 1 <= args.cycles <= 100:
        parser.error("--cycles must be in the range 1..100")
    if args.power_off_seconds < 10:
        parser.error("--power-off-seconds must be at least 10")
    if args.break_seconds is None:
        args.break_seconds = 2.0 if args.case == "R05A" else 10.0
    if args.break_seconds <= 0:
        parser.error("--break-seconds must be positive")
    if args.case == "R05B" and args.break_seconds < 8.0:
        parser.error("R05B requires --break-seconds of at least 8 seconds")
    if not args.confirm_destructive:
        parser.error("these tests can erase STM32 APP; add --confirm-destructive")
    return args


def execute(args: argparse.Namespace) -> tuple[dict, Path, Path]:
    case_id = args.case.upper()
    log_path = (args.log or DEFAULT_BUILD / f"m7_{case_id.lower()}_result.txt").resolve()
    report_path = (
        args.report or DEFAULT_BUILD / f"m7_{case_id.lower()}_report.md"
    ).resolve()

    if case_id == "R09":
        result = run_r09(args, log_path)
        return result, log_path, report_path

    console = GatewayConsole(
        args.port,
        args.baud,
        log_path,
        label="M7 guided reliability test",
    )
    try:
        console.synchronize()
        if case_id == "R05A":
            result = run_r05(console, args, expect_exhaustion=False)
        elif case_id == "R05B":
            result = run_r05(console, args, expect_exhaustion=True)
        else:
            result = run_r06(console, args)
        console.record(f"\n[PASS] {case_id} guided reliability test\n")
        return result, log_path, report_path
    except Exception:
        diagnostics = safe_diagnostics(console)
        console.record("\n[DIAGNOSTICS]\n")
        for name, value in diagnostics.items():
            console.record(f"{name}:\n{value}\n")
        raise
    finally:
        console.close()


def main() -> None:
    args = parse_args()
    try:
        result, log_path, report_path = execute(args)
    except (EOFError, KeyboardInterrupt) as exc:
        print("\n[STOP] guided test cancelled by user", file=sys.stderr)
        raise SystemExit(130) from exc
    except Exception as exc:
        print(f"\n[FAIL] {exc}", file=sys.stderr)
        log_path = (
            args.log or DEFAULT_BUILD / f"m7_{args.case.lower()}_result.txt"
        ).resolve()
        report_path = (
            args.report or DEFAULT_BUILD / f"m7_{args.case.lower()}_report.md"
        ).resolve()
        if log_path.is_file():
            digest = hashlib.sha256(log_path.read_bytes()).hexdigest().upper()
            write_report(
                report_path,
                args.case,
                {"result": "FAIL", "sessions": [], "evidence": [str(exc)]},
                args,
                digest,
            )
            print(f"Log: {log_path}", file=sys.stderr)
            print(f"Log SHA-256: {digest}", file=sys.stderr)
            print(f"Report: {report_path}", file=sys.stderr)
        raise SystemExit(1) from exc

    digest = hashlib.sha256(log_path.read_bytes()).hexdigest().upper()
    write_report(report_path, args.case, result, args, digest)
    print(f"\n[PASS] {args.case}")
    print(f"Log: {log_path}")
    print(f"Log SHA-256: {digest}")
    print(f"Report: {report_path}")


if __name__ == "__main__":
    main()
