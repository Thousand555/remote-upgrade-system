from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "server"))

from app.range_requests import RangeNotSatisfiable, parse_single_range  # noqa: E402


class RangeRequestTests(unittest.TestCase):
    def test_missing_range_requests_the_full_resource(self):
        self.assertIsNone(parse_single_range(None, 100))

    def test_normal_open_ended_and_suffix_ranges(self):
        selected = parse_single_range("bytes=10-19", 100)
        self.assertEqual((selected.start, selected.end), (10, 19))
        selected = parse_single_range("bytes=95-", 100)
        self.assertEqual((selected.start, selected.end), (95, 99))
        selected = parse_single_range("bytes=-12", 100)
        self.assertEqual((selected.start, selected.end), (88, 99))

    def test_ranges_are_clamped_or_rejected_as_required_by_http(self):
        selected = parse_single_range("bytes=90-200", 100)
        self.assertEqual((selected.start, selected.end), (90, 99))
        for header in ("bytes=100-101", "bytes=9-8", "bytes=0-1,4-5", "items=0-1"):
            with self.subTest(header=header):
                with self.assertRaises(RangeNotSatisfiable):
                    parse_single_range(header, 100)
