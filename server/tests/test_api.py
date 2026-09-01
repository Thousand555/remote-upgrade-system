from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "server"))
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

from fastapi.testclient import TestClient  # noqa: E402

from app.main import create_app  # noqa: E402
from pack_firmware import build_release  # noqa: E402


class FirmwareApiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.firmware_root = Path(self.temporary.name)
        self.firmware_id = "f407-node-1.2.0"
        self.package, self.manifest = build_release(
            image=b"M8 HTTP test image",
            firmware_id=self.firmware_id,
            firmware_version=2,
            product_id=1,
            hardware_id=1,
            display_version="1.2.0",
            source_commit="a" * 40,
            source_filename="stm32_app.bin",
            created_at="2026-09-01T00:00:00Z",
        )
        release_directory = self.firmware_root / self.firmware_id
        release_directory.mkdir()
        (release_directory / "firmware.bin").write_bytes(self.package)
        (release_directory / "manifest.json").write_text(
            json.dumps(self.manifest), encoding="utf-8"
        )
        self.client = TestClient(create_app(self.firmware_root))

    def tearDown(self):
        self.temporary.cleanup()

    def test_manifest_and_full_binary_are_served_with_an_etag(self):
        manifest_response = self.client.get(f"/api/v1/firmwares/{self.firmware_id}/manifest")
        self.assertEqual(manifest_response.status_code, 200)
        self.assertEqual(manifest_response.json(), self.manifest)

        binary_response = self.client.get(f"/api/v1/firmwares/{self.firmware_id}/binary")
        self.assertEqual(binary_response.status_code, 200)
        self.assertEqual(binary_response.content, self.package)
        self.assertEqual(binary_response.headers["etag"], manifest_response.headers["etag"])
        self.assertEqual(binary_response.headers["accept-ranges"], "bytes")

    def test_range_and_if_range_behavior_is_safe_for_resume(self):
        endpoint = f"/api/v1/firmwares/{self.firmware_id}/binary"
        etag = f'"{self.manifest["package_sha256"].lower()}"'
        partial_response = self.client.get(
            endpoint,
            headers={"Range": "bytes=4096-4103", "If-Range": etag},
        )
        self.assertEqual(partial_response.status_code, 206)
        self.assertEqual(partial_response.content, self.package[4096:4104])
        self.assertEqual(
            partial_response.headers["content-range"], f"bytes 4096-4103/{len(self.package)}"
        )

        changed_response = self.client.get(
            endpoint,
            headers={"Range": "bytes=4096-4103", "If-Range": '"old-release"'},
        )
        self.assertEqual(changed_response.status_code, 200)
        self.assertEqual(changed_response.content, self.package)

    def test_invalid_range_and_missing_release_have_safe_http_statuses(self):
        endpoint = f"/api/v1/firmwares/{self.firmware_id}/binary"
        invalid_response = self.client.get(endpoint, headers={"Range": "bytes=999999-"})
        self.assertEqual(invalid_response.status_code, 416)
        self.assertEqual(invalid_response.headers["content-range"], f"bytes */{len(self.package)}")
        self.assertEqual(self.client.get("/api/v1/firmwares/no-such-release/manifest").status_code, 404)

    def test_test_only_stream_delay_is_explicit_and_bounded(self):
        endpoint = f"/api/v1/firmwares/{self.firmware_id}/binary"
        delayed_client = TestClient(create_app(self.firmware_root, test_stream_delay_ms=25))
        with mock.patch("app.main.STREAM_BLOCK_SIZE", 8), mock.patch(
            "app.main.time.sleep"
        ) as sleep:
            response = delayed_client.get(endpoint)

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.content, self.package)
        self.assertGreater(sleep.call_count, 0)
        sleep.assert_called_with(0.025)
        with self.assertRaisesRegex(ValueError, "test stream delay"):
            create_app(self.firmware_root, test_stream_delay_ms=5001)
