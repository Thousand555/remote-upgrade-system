import unittest

from run_m7_reliability import (
    parse_bootloader_probe,
    parse_case_ids,
    parse_upgrade_status,
    render_markdown_report,
    require_all_faults_off,
    require_failed_status,
    require_fault_state,
)


FAILED_STATUS = """
State: FAILED
Session: 0x1234ABCD
Firmware version: 1
Progress: 12888/12888 bytes
Remote boot state: 8
Last result: ESP_ERR_INVALID_CRC, device status=11
gateway>
"""

BOOTLOADER_PROBE = """
Service: capabilities=0x0001, version=0x00010000
Device: product=0x0001, hardware=0x0001, boot_state=8
Versions: bootloader=0x00010000, application=1
APP: base=0x08020000, max_size=0x000E0000
gateway>
"""

FAULTS_OFF = """
Reliability fault injection: ENABLED (test build only)
  drop_data_ack_once         : off
  duplicate_data_once        : off
  gap_offset_once            : off
  bad_manifest_crc_once      : off
  drop_activate_ack_once     : off
  timeout                    : off
gateway>
"""


class M7ReliabilityParserTests(unittest.TestCase):
    def test_parse_failed_upgrade_status(self):
        status = parse_upgrade_status(FAILED_STATUS)
        self.assertEqual(status.state, "FAILED")
        self.assertEqual(status.session, "0X1234ABCD")
        self.assertEqual(status.error, "ESP_ERR_INVALID_CRC")
        self.assertEqual(status.device_status, 11)
        self.assertEqual(status.remote_boot_state, 8)

    def test_require_failed_status_checks_error_and_device_status(self):
        require_failed_status(FAILED_STATUS, "ESP_ERR_INVALID_CRC", 11)
        with self.assertRaises(RuntimeError):
            require_failed_status(FAILED_STATUS, "ESP_ERR_TIMEOUT")
        with self.assertRaises(RuntimeError):
            require_failed_status(FAILED_STATUS, "ESP_ERR_INVALID_CRC", 0)

    def test_parse_bootloader_probe(self):
        probe = parse_bootloader_probe(BOOTLOADER_PROBE)
        self.assertEqual(probe.capabilities, 1)
        self.assertEqual(probe.boot_state, 8)
        with self.assertRaises(RuntimeError):
            parse_bootloader_probe(
                BOOTLOADER_PROBE.replace("capabilities=0x0001", "capabilities=0x0002")
            )

    def test_fault_state_parsing(self):
        require_all_faults_off(FAULTS_OFF)
        armed = FAULTS_OFF.replace("duplicate_data_once        : off", "duplicate_data_once        : armed")
        require_fault_state(armed, "duplicate_data_once", "armed")
        with self.assertRaises(RuntimeError):
            require_all_faults_off(armed)

    def test_timeout_fault_state_parsing(self):
        armed = FAULTS_OFF.replace(
            "timeout                    : off",
            "timeout                    : command=0x12, remaining=5",
        )
        require_fault_state(armed, "timeout", "command=0x12, remaining=5")

    def test_case_list_is_normalized_and_deduplicated(self):
        self.assertEqual(parse_case_ids("r11,R12,r11,R15"), ["R11", "R12", "R15"])

    def test_markdown_report_contains_case_analysis(self):
        report = {
            "result": "PASS",
            "started_at": "2026-08-31T10:00:00+0800",
            "finished_at": "2026-08-31T10:01:00+0800",
            "port": "COM9",
            "baud": 115200,
            "expected_version": 1,
            "image_size": 12888,
            "passed": 1,
            "failed": 0,
            "requested_cases": ["R11"],
            "log_path": "result.txt",
            "log_sha256": "ABC",
            "cases": [
                {
                    "id": "R11",
                    "result": "PASS",
                    "elapsed_seconds": 6.4,
                    "sessions": ["0X1234ABCD"],
                    "analysis": "duplicate DATA is idempotent",
                    "evidence": ["offset unchanged"],
                }
            ],
        }
        markdown = render_markdown_report(report)
        self.assertIn("| R11 | PASS | 6.400 | 0X1234ABCD |", markdown)
        self.assertIn("offset unchanged", markdown)


if __name__ == "__main__":
    unittest.main()
