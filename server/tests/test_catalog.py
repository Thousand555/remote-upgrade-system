from __future__ import annotations

import binascii
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "server"))
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

from app.catalog import CatalogError, FirmwareCatalog  # noqa: E402
from firmware_package import build_package  # noqa: E402


def make_manifest(firmware_id: str, package: bytes) -> dict[str, object]:
    image = package[0x1000:]
    return {
        "schema_version": 1,
        "firmware_id": firmware_id,
        "product_id": 1,
        "hardware_id": 1,
        "firmware_version": "2",
        "firmware_version_code": 2,
        "app_base": "0x08020000",
        "image_size": len(image),
        "crc32": f"{binascii.crc32(image) & 0xFFFFFFFF:08X}",
        "sha256": hashlib.sha256(image).hexdigest().upper(),
        "package_format": 1,
        "package_header_size": 128,
        "package_image_offset": 0x1000,
        "package_size": len(package),
        "package_crc32": f"{binascii.crc32(package) & 0xFFFFFFFF:08X}",
        "package_sha256": hashlib.sha256(package).hexdigest().upper(),
        "download_url": f"/api/v1/firmwares/{firmware_id}/binary",
    }


class FirmwareCatalogTests(unittest.TestCase):
    def write_release(self, root: Path, firmware_id: str = "f407-node-1.2.0") -> tuple[Path, bytes]:
        package = build_package(b"M8 firmware image", 2)
        release_directory = root / firmware_id
        release_directory.mkdir(parents=True)
        (release_directory / "firmware.bin").write_bytes(package)
        (release_directory / "manifest.json").write_text(
            json.dumps(make_manifest(firmware_id, package)), encoding="utf-8"
        )
        return release_directory, package

    def test_valid_release_loads_and_uses_package_hash_as_etag(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, package = self.write_release(root)
            record = FirmwareCatalog(root).get("f407-node-1.2.0")

            self.assertIsNotNone(record)
            assert record is not None
            self.assertEqual(record.package_size, len(package))
            self.assertEqual(record.etag, f'"{hashlib.sha256(package).hexdigest()}"')

    def test_corrupted_binary_is_not_served(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            release_directory, _ = self.write_release(root)
            binary_path = release_directory / "firmware.bin"
            binary_path.write_bytes(binary_path.read_bytes() + b"x")

            with self.assertRaisesRegex(CatalogError, "size"):
                FirmwareCatalog(root).get("f407-node-1.2.0")

    def test_path_traversal_and_unknown_releases_are_not_resolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            catalog = FirmwareCatalog(Path(temporary))
            self.assertIsNone(catalog.get("../firmware"))
            self.assertIsNone(catalog.get("does-not-exist"))
