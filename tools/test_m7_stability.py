import unittest

from run_m7_stability import (
    clean_console_text,
    require_application_probe,
    require_firmware_validation,
    require_success_status,
    terminal_query_response,
    GatewayConsole,
)


VALIDATE_OUTPUT = """
Firmware version: 1
Image size: 12888 bytes
Image CRC32: 0x8A0EB599
Product/Hardware: 0x0001/0x0001
gateway>
"""

STATUS_OUTPUT = """
State: SUCCESS
Session: 0xE5869149
Firmware version: 1
Progress: 12888/12888 bytes
Remote boot state: 6
Last result: ESP_OK, device status=0
gateway>
"""

PROBE_OUTPUT = """
Service: capabilities=0x0002, version=0x00000001
Device: product=0x0001, hardware=0x0001, boot_state=6
Versions: bootloader=0x00010000, application=1
APP: base=0x08020000, max_size=0x000E0000
gateway>
"""


class M7StabilityParserTests(unittest.TestCase):
    def test_clean_console_text_removes_ansi_and_cr(self):
        self.assertEqual(
            clean_console_text("\x1b[31mOK\x1b[0m\r\n\x00"), "OK\n"
        )

    def test_cursor_query_gets_noninteractive_fallback(self):
        tail, response = terminal_query_response(b"", b"gateway> \x1b[6n\x00")
        self.assertEqual(response, b"\x1b[R")
        self.assertEqual(tail, b"6n\x00")

    def test_cursor_query_can_span_serial_reads(self):
        tail, response = terminal_query_response(b"", b"\x1b[")
        self.assertEqual(response, b"")
        tail, response = terminal_query_response(tail, b"6n")
        self.assertEqual(response, b"\x1b[R")

    def test_final_prompt_ignores_ansi_redraw_prompt(self):
        console = GatewayConsole.__new__(GatewayConsole)
        chunks = iter(
            (
                "\rgateway> f",
                "irmware validate",
                "\nFirmware version: 2\n",
                "gateway> ",
            )
        )

        def read_available():
            try:
                return next(chunks)
            except StopIteration:
                return ""

        console._read_available = read_available
        output = console.read_until_prompt("firmware validate", 0.1)
        self.assertIn("Firmware version: 2", output)

    def test_valid_cycle_outputs(self):
        size = require_firmware_validation(VALIDATE_OUTPUT, 1)
        self.assertEqual(size, 12888)
        self.assertEqual(require_success_status(STATUS_OUTPUT, size, 1), "0XE5869149")
        require_application_probe(PROBE_OUTPUT, 1)

    def test_status_rejects_partial_progress(self):
        with self.assertRaises(RuntimeError):
            require_success_status(
                STATUS_OUTPUT.replace("12888/12888", "8192/12888"),
                12888,
                1,
            )

    def test_probe_rejects_bootloader_capability(self):
        with self.assertRaises(RuntimeError):
            require_application_probe(
                PROBE_OUTPUT.replace("capabilities=0x0002", "capabilities=0x0001"),
                1,
            )


if __name__ == "__main__":
    unittest.main()
