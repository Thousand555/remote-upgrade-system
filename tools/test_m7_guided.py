import unittest

from run_m7_guided import require_uart_retry_evidence


class M7GuidedParserTests(unittest.TestCase):
    def test_uart_retry_evidence_accepts_timeout_and_decode_failure(self):
        output = """
W upgrade_client: Command 0x12 attempt 1/5 failed: ESP_ERR_TIMEOUT
W upgrade_client: Command 0x12 attempt 2/5 decode failed: protocol=3, length=1
"""
        self.assertEqual(require_uart_retry_evidence(output), [1, 2])

    def test_uart_retry_evidence_rejects_normal_transfer(self):
        with self.assertRaises(RuntimeError):
            require_uart_retry_evidence("Transferred 4096/917504 bytes")

    def test_uart_retry_evidence_ignores_non_data_commands(self):
        with self.assertRaises(RuntimeError):
            require_uart_retry_evidence(
                "Command 0x13 attempt 1/1 failed: ESP_ERR_TIMEOUT"
            )


if __name__ == "__main__":
    unittest.main()
