"""Run and analyze M7 software fault-injection tests through the ESP32 console."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

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
DEFAULT_LOG = REPOSITORY_ROOT / "build" / "m7_reliability_result.txt"
DEFAULT_JSON = REPOSITORY_ROOT / "build" / "m7_reliability_result.json"
DEFAULT_REPORT = REPOSITORY_ROOT / "build" / "m7_reliability_report.md"
CASE_IDS = ("R10", "R11", "R12", "R13", "R14", "R15")
CASE_ANALYSIS = {
    "R10": "DATA应答丢失后重发同一数据块，STM32按幂等规则应答并完成升级。",
    "R11": "重复DATA未重复推进Offset，随后正常完成升级。",
    "R12": "跨越期望Offset的数据被拒绝，ESP32按设备返回的Offset继续传输。",
    "R13": "错误Manifest CRC在VERIFY阶段被拒绝，未进入ACTIVATE；正确CRC重试恢复。",
    "R14": "ACTIVATE有效应答丢失后不重复激活，通过APP回探消除结果歧义。",
    "R15": "4次事务超时由内部重试恢复；5次耗尽后保留会话并由下一次升级续传。",
}


@dataclass(frozen=True)
class UpgradeStatus:
    state: str
    session: str
    firmware_version: int
    transferred: int
    image_size: int
    remote_boot_state: int
    error: str
    device_status: int


@dataclass(frozen=True)
class BootloaderProbe:
    capabilities: int
    product_id: int
    hardware_id: int
    boot_state: int
    application_version: int


def parse_upgrade_status(output: str) -> UpgradeStatus:
    """Parse an ``upgrade status`` response without deciding pass/fail."""
    text = clean_console_text(output)
    patterns = {
        "state": r"State:\s*(\w+)",
        "session": r"Session:\s*(0x[0-9A-Fa-f]{8})",
        "version": r"Firmware version:\s*(\d+)",
        "progress": r"Progress:\s*(\d+)/(\d+)\s+bytes",
        "remote": r"Remote boot state:\s*(\d+)",
        "result": r"Last result:\s*([A-Z0-9_]+),\s*device status=(\d+)",
    }
    matches = {name: re.search(pattern, text) for name, pattern in patterns.items()}
    missing = [name for name, match in matches.items() if match is None]
    if missing:
        raise RuntimeError("upgrade status output is incomplete: " + ", ".join(missing))

    state = matches["state"]
    session = matches["session"]
    version = matches["version"]
    progress = matches["progress"]
    remote = matches["remote"]
    result = matches["result"]
    assert state and session and version and progress and remote and result
    return UpgradeStatus(
        state=state.group(1),
        session=session.group(1).upper(),
        firmware_version=int(version.group(1)),
        transferred=int(progress.group(1)),
        image_size=int(progress.group(2)),
        remote_boot_state=int(remote.group(1)),
        error=result.group(1),
        device_status=int(result.group(2)),
    )


def require_failed_status(
    output: str,
    expected_error: str,
    expected_device_status: Optional[int] = None,
) -> UpgradeStatus:
    status = parse_upgrade_status(output)
    if status.state != "FAILED":
        raise RuntimeError(f"expected FAILED, got {status.state}")
    if status.error != expected_error:
        raise RuntimeError(f"expected {expected_error}, got {status.error}")
    if (
        expected_device_status is not None
        and status.device_status != expected_device_status
    ):
        raise RuntimeError(
            f"expected device status {expected_device_status}, "
            f"got {status.device_status}"
        )
    return status


def parse_bootloader_probe(output: str) -> BootloaderProbe:
    """Parse a probe response and require the STM32 Bootloader service."""
    text = clean_console_text(output)
    service = re.search(r"Service:\s*capabilities=0x([0-9A-Fa-f]{4})", text)
    identity = re.search(
        r"Device:\s*product=0x([0-9A-Fa-f]{4}),\s*"
        r"hardware=0x([0-9A-Fa-f]{4}),\s*boot_state=(\d+)",
        text,
    )
    app_version = re.search(r"application=(\d+)", text)
    if service is None or identity is None or app_version is None:
        raise RuntimeError("upgrade probe output is incomplete")
    probe = BootloaderProbe(
        capabilities=int(service.group(1), 16),
        product_id=int(identity.group(1), 16),
        hardware_id=int(identity.group(2), 16),
        boot_state=int(identity.group(3)),
        application_version=int(app_version.group(1)),
    )
    if probe.capabilities & 0x0001 == 0:
        raise RuntimeError("probe did not discover the STM32 Bootloader service")
    if (probe.product_id, probe.hardware_id) != (1, 1):
        raise RuntimeError("STM32 Product/Hardware mismatch")
    return probe


def require_fault_state(output: str, name: str, expected: str) -> None:
    text = clean_console_text(output)
    match = re.search(
        rf"^\s*{re.escape(name)}\s*:\s*(armed|off|command=0x[0-9A-Fa-f]+,\s*remaining=\d+)",
        text,
        re.MULTILINE,
    )
    if match is None:
        raise RuntimeError(f"fault status {name!r} is missing; use reliability-test firmware")
    if match.group(1) != expected:
        raise RuntimeError(
            f"fault {name} expected {expected!r}, got {match.group(1)!r}"
        )


def require_all_faults_off(output: str) -> None:
    for name in (
        "drop_data_ack_once",
        "duplicate_data_once",
        "gap_offset_once",
        "bad_manifest_crc_once",
        "drop_activate_ack_once",
        "timeout",
    ):
        require_fault_state(output, name, "off")


class ReliabilityRunner:
    def __init__(
        self,
        console: GatewayConsole,
        image_size: int,
        version: int,
        timeout: float,
    ):
        self.console = console
        self.image_size = image_size
        self.version = version
        self.timeout = timeout

    def clear_faults(self) -> None:
        output = self.console.command("test fault clear")
        if "All reliability faults cleared." not in clean_console_text(output):
            raise RuntimeError("fault-clear command unavailable; flash reliability-test firmware")
        require_all_faults_off(self.console.command("test fault show"))

    def arm_fault(self, name: str) -> None:
        output = self.console.command(f"test fault {name}")
        if f"Armed one-shot fault: {name}" not in clean_console_text(output):
            raise RuntimeError(f"failed to arm {name}")
        require_fault_state(self.console.command("test fault show"), name, "armed")

    def arm_timeout(self, command: str, count: int, command_id: int) -> None:
        output = self.console.command(f"test fault timeout {command} {count}")
        marker = f"Armed timeout: command=0x{command_id:02X}, count={count}"
        if marker not in clean_console_text(output):
            raise RuntimeError(f"failed to arm {command} timeout count {count}")
        require_fault_state(
            self.console.command("test fault show"),
            "timeout",
            f"command=0x{command_id:02X}, remaining={count}",
        )

    def start_and_wait(self) -> str:
        start = self.console.command("upgrade start")
        if "Command accepted." not in clean_console_text(start):
            raise RuntimeError("gateway rejected upgrade start")
        return clean_console_text(self.console.wait_for_upgrade(start, self.timeout))

    def require_success(self, outcome: str) -> UpgradeStatus:
        if FAILURE_MARKER in outcome:
            raise RuntimeError("gateway reported an unexpected upgrade failure")
        if SUCCESS_MARKER not in outcome:
            raise RuntimeError("gateway success marker is missing")
        status_output = self.console.command("upgrade status")
        session = require_success_status(status_output, self.image_size, self.version)
        status = parse_upgrade_status(status_output)
        if status.session != session:
            raise RuntimeError("status parser session mismatch")
        require_application_probe(self.console.command("upgrade probe"), self.version)
        require_all_faults_off(self.console.command("test fault show"))
        return status

    def require_expected_failure(
        self,
        outcome: str,
        error: str,
        device_status: Optional[int] = None,
    ) -> UpgradeStatus:
        if SUCCESS_MARKER in outcome:
            raise RuntimeError("gateway unexpectedly reported upgrade success")
        if FAILURE_MARKER not in outcome:
            raise RuntimeError("gateway failure marker is missing")
        return require_failed_status(
            self.console.command("upgrade status"), error, device_status
        )

    def recover_to_application(self) -> UpgradeStatus:
        outcome = self.start_and_wait()
        return self.require_success(outcome)

    def run_r10(self) -> dict:
        self.arm_fault("drop_data_ack_once")
        outcome = self.start_and_wait()
        marker = "TEST: discarded one valid DATA response at offset 0"
        if marker not in outcome:
            raise RuntimeError("DATA response-loss marker is missing")
        status = self.require_success(outcome)
        return {
            "sessions": [status.session],
            "evidence": [marker, "same DATA retried and full image activated"],
        }

    def run_r11(self) -> dict:
        self.arm_fault("duplicate_data_once")
        outcome = self.start_and_wait()
        marker = "TEST: duplicate DATA at offset 0 accepted without progress change"
        if marker not in outcome:
            raise RuntimeError("duplicate-DATA idempotency marker is missing")
        status = self.require_success(outcome)
        return {
            "sessions": [status.session],
            "evidence": [marker, "duplicate did not advance offset"],
        }

    def run_r12(self) -> dict:
        self.arm_fault("gap_offset_once")
        outcome = self.start_and_wait()
        marker = re.search(r"TEST: gap offset \d+ rejected; device requested \d+", outcome)
        if marker is None:
            raise RuntimeError("gap-offset rejection marker is missing")
        status = self.require_success(outcome)
        return {
            "sessions": [status.session],
            "evidence": [marker.group(0), "transfer resumed from requested offset"],
        }

    def run_r13(self) -> dict:
        self.arm_fault("bad_manifest_crc_once")
        outcome = self.start_and_wait()
        crc_marker = re.search(
            r"TEST: START manifest CRC32 overridden to 0x[0-9A-Fa-f]{8}", outcome
        )
        if crc_marker is None:
            raise RuntimeError("bad-manifest CRC marker is missing")
        if "State -> ACTIVATE" in outcome:
            raise RuntimeError("ACTIVATE was entered after a failed CRC verification")
        failed = self.require_expected_failure(outcome, "ESP_ERR_INVALID_CRC", 11)
        if failed.transferred != self.image_size:
            raise RuntimeError("CRC-negative test did not transfer the complete image")
        boot = parse_bootloader_probe(self.console.command("upgrade probe"))
        if boot.boot_state != 8:
            raise RuntimeError(f"expected Bootloader FAILED state 8, got {boot.boot_state}")
        require_all_faults_off(self.console.command("test fault show"))

        recovered = self.recover_to_application()
        return {
            "sessions": [failed.session, recovered.session],
            "evidence": [
                crc_marker.group(0),
                "VERIFY_FAILED/device status 11; ACTIVATE not entered",
                "clean retry recovered to APP",
            ],
        }

    def run_r14(self) -> dict:
        self.arm_fault("drop_activate_ack_once")
        outcome = self.start_and_wait()
        markers = (
            "TEST: discarded the valid ACTIVATE response",
            "ACTIVATE response was ambiguous",
        )
        for marker in markers:
            if marker not in outcome:
                raise RuntimeError(f"ACTIVATE ambiguity marker is missing: {marker}")
        status = self.require_success(outcome)
        return {
            "sessions": [status.session],
            "evidence": [*markers, "APP probe resolved ambiguous ACTIVATE as success"],
        }

    def run_r15(self) -> dict:
        timeout_marker = "TEST: command 0x12 attempt"

        self.arm_timeout("data", 4, 0x12)
        recoverable_outcome = self.start_and_wait()
        if recoverable_outcome.count(timeout_marker) != 4:
            raise RuntimeError("R15 recoverable phase did not inject exactly 4 timeouts")
        first = self.require_success(recoverable_outcome)

        self.arm_timeout("data", 5, 0x12)
        exhausted_outcome = self.start_and_wait()
        if exhausted_outcome.count(timeout_marker) != 5:
            raise RuntimeError("R15 exhausted phase did not inject exactly 5 timeouts")
        failed = self.require_expected_failure(exhausted_outcome, "ESP_ERR_TIMEOUT")
        boot = parse_bootloader_probe(self.console.command("upgrade probe"))
        if boot.boot_state != 4:
            raise RuntimeError(
                f"expected resumable RECEIVING state 4, got {boot.boot_state}"
            )
        require_all_faults_off(self.console.command("test fault show"))

        resumed = self.recover_to_application()
        if resumed.session != failed.session:
            raise RuntimeError(
                f"resume changed session {failed.session} -> {resumed.session}"
            )
        return {
            "sessions": [first.session, failed.session, resumed.session],
            "evidence": [
                "4/5 DATA timeouts recovered inside transaction",
                "5/5 DATA timeouts produced ESP_ERR_TIMEOUT",
                "Bootloader remained RECEIVING and clean retry resumed the same session",
            ],
        }


def parse_case_ids(value: str) -> list[str]:
    result: list[str] = []
    for item in value.split(","):
        case_id = item.strip().upper()
        if not case_id:
            continue
        if case_id not in CASE_IDS:
            raise argparse.ArgumentTypeError(
                f"unknown case {case_id}; choose from {','.join(CASE_IDS)}"
            )
        if case_id not in result:
            result.append(case_id)
    if not result:
        raise argparse.ArgumentTypeError("at least one case is required")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run M7 R10-R15 reliability tests and generate a JSON report"
    )
    parser.add_argument("--port", required=True, help="ESP32 console COM port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--version", type=int, default=1)
    parser.add_argument("--cases", type=parse_case_ids, default=list(CASE_IDS))
    parser.add_argument("--case-timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--confirm-destructive", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.version <= 0xFFFFFFFF:
        parser.error("--version must be in the range 1..0xFFFFFFFF")
    if args.case_timeout <= 0:
        parser.error("--case-timeout must be positive")
    if not args.confirm_destructive:
        parser.error("these tests erase STM32 APP repeatedly; add --confirm-destructive")
    return args


def safe_diagnostics(console: GatewayConsole) -> dict:
    diagnostics: dict[str, str] = {}
    for name, command in (
        ("upgrade_status", "upgrade status"),
        ("upgrade_probe", "upgrade probe"),
        ("faults", "test fault show"),
    ):
        try:
            diagnostics[name] = clean_console_text(console.command(command, 12.0)).strip()
        except Exception as exc:  # Preserve the primary failure.
            diagnostics[name] = f"diagnostic failed: {exc}"
    return diagnostics


def execute(args: argparse.Namespace) -> tuple[dict, bool]:
    log_path = args.log.resolve()
    console = GatewayConsole(
        args.port,
        args.baud,
        log_path,
        label="M7 software fault-injection test",
    )
    report: dict = {
        "suite": "M7 R10-R15 software fault injection",
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "port": args.port,
        "baud": args.baud,
        "expected_version": args.version,
        "requested_cases": args.cases,
        "cases": [],
    }
    all_passed = True
    runner: Optional[ReliabilityRunner] = None
    try:
        console.synchronize()
        validate = console.command("firmware validate")
        image_size = require_firmware_validation(validate, args.version)
        report["image_size"] = image_size
        require_application_probe(console.command("upgrade probe"), args.version)
        runner = ReliabilityRunner(console, image_size, args.version, args.case_timeout)
        runner.clear_faults()

        methods: dict[str, Callable[[], dict]] = {
            "R10": runner.run_r10,
            "R11": runner.run_r11,
            "R12": runner.run_r12,
            "R13": runner.run_r13,
            "R14": runner.run_r14,
            "R15": runner.run_r15,
        }
        for case_id in args.cases:
            console.record(f"\n========== {case_id} ==========" + "\n")
            started = time.monotonic()
            case_result: dict = {"id": case_id}
            try:
                runner.clear_faults()
                case_result.update(methods[case_id]())
                case_result["result"] = "PASS"
                case_result["analysis"] = CASE_ANALYSIS[case_id]
                case_result["elapsed_seconds"] = round(time.monotonic() - started, 3)
                console.record(
                    f"\n[PASS] {case_id}: elapsed="
                    f"{case_result['elapsed_seconds']:.3f}s\n"
                )
                console.record(f"Analysis: {case_result['analysis']}\n")
                for evidence in case_result.get("evidence", []):
                    console.record(f"  - {evidence}\n")
            except Exception as exc:
                all_passed = False
                case_result["result"] = "FAIL"
                case_result["elapsed_seconds"] = round(time.monotonic() - started, 3)
                case_result["error"] = str(exc)
                case_result["analysis"] = (
                    "未满足自动判据；已停止后续用例并采集状态、探测和故障配置诊断。"
                )
                console.record(f"\n[FAIL] {case_id}: {exc}\n")
                case_result["diagnostics"] = safe_diagnostics(console)
            report["cases"].append(case_result)
            if not all_passed:
                break
    except Exception as exc:
        all_passed = False
        report["setup_error"] = str(exc)
        console.record(f"\n[FAIL] suite setup: {exc}\n")
        report["setup_diagnostics"] = safe_diagnostics(console)
    finally:
        if runner is not None:
            try:
                runner.clear_faults()
            except Exception as exc:
                report["cleanup_error"] = str(exc)
                all_passed = False
        console.close()

    report["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    report["passed"] = sum(case["result"] == "PASS" for case in report["cases"])
    report["failed"] = sum(case["result"] == "FAIL" for case in report["cases"])
    report["result"] = "PASS" if all_passed else "FAIL"
    report["log_path"] = str(log_path)
    report["log_sha256"] = hashlib.sha256(log_path.read_bytes()).hexdigest().upper()
    return report, all_passed


def render_markdown_report(report: dict) -> str:
    lines = [
        "# M7可靠性自动测试报告",
        "",
        f"- 总体结果：**{report['result']}**",
        f"- 测试时间：{report['started_at']} ～ {report['finished_at']}",
        f"- 串口：{report['port']} @ {report['baud']}",
        f"- 固件版本/镜像大小：{report['expected_version']} / {report.get('image_size', '未知')} bytes",
        f"- 通过：{report['passed']}；失败：{report['failed']}；计划：{len(report['requested_cases'])}",
        f"- 原始日志：`{report['log_path']}`",
        f"- 日志SHA-256：`{report['log_sha256']}`",
        "",
        "| 用例 | 结果 | 耗时(s) | Session | 自动分析 |",
        "| --- | --- | ---: | --- | --- |",
    ]
    for case in report["cases"]:
        sessions = ", ".join(case.get("sessions", [])) or "-"
        analysis = case.get("analysis", "-").replace("|", "\\|")
        lines.append(
            f"| {case['id']} | {case['result']} | {case['elapsed_seconds']:.3f} | "
            f"{sessions} | {analysis} |"
        )

    if report.get("setup_error"):
        lines.extend(["", "## 初始化失败", "", report["setup_error"]])
    for case in report["cases"]:
        lines.extend(["", f"## {case['id']}：{case['result']}", ""])
        lines.append(case.get("analysis", ""))
        if case.get("error"):
            lines.extend(["", f"错误：`{case['error']}`"])
        if case.get("evidence"):
            lines.extend(["", "证据：", ""])
            lines.extend(f"- {item}" for item in case["evidence"])
        if case.get("diagnostics"):
            lines.extend(["", "失败现场诊断：", ""])
            for name, text in case["diagnostics"].items():
                lines.extend([f"### {name}", "", "```text", text, "```", ""])
    if report.get("cleanup_error"):
        lines.extend(["", "## 清理异常", "", report["cleanup_error"]])
    lines.extend(
        [
            "",
            "## 范围说明",
            "",
            "本报告只覆盖R10～R15软件故障注入。R02～R06的复位、断电测试需要人工操作，"
            "除非另接可编程电源或复位控制器。",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    json_path = args.json.resolve()
    report_path = args.report.resolve()
    json_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report, passed = execute(args)
    json_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    report_path.write_text(render_markdown_report(report), encoding="utf-8")

    print(
        f"\n[{report['result']}] M7 reliability: "
        f"{report['passed']}/{len(report['requested_cases'])} cases passed"
    )
    print(f"Log: {report['log_path']}")
    print(f"Log SHA-256: {report['log_sha256']}")
    print(f"JSON: {json_path}")
    print(f"Report: {report_path}")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
